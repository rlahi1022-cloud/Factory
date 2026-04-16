"""StationRunner.py
AI 추론 서버의 비동기 큐 파이프라인 베이스.

파이프라인:
  [Pylon Grab Producer] -> grab_queue -> [Inference Worker(s)] -> result_queue -> [Sender Worker(s)] -> 메인 서버
  [OK Stat Reporter] (별도 코루틴) -> 주기적 STATION_OK_COUNT(1004) 송신
  [Inference Worker] -> INSPECT_META(1006) 송신 (OK/NG 공통)

설계 원칙:
- 모든 단계는 asyncio.Queue로 분리 → 단계별 백프레셔/병렬도 독립 조정.
- 추론은 CPU/GPU 바운드이므로 loop.run_in_executor 로 별도 스레드에서 실행.
- NG는 ACK 기반 송신, OK는 카운터만 누적 후 주기 송신.
- inspection_id는 'stationN-YYYYMMDDHHMMSSmmm-seq' 형식으로 추론서버에서 발급.
"""

from __future__ import annotations

import asyncio
import logging
import time
from datetime import datetime, timezone
from typing import Any, Optional

from Common.Config import StationConfig
from Common.Inferencer import BaseInferencer
from Common.Packet import PacketBuilder
from Common.Protocol import ProtocolNo
from Common.SerialCtrl import SerialCtrl
from Common.TcpClient import TcpClient


logger = logging.getLogger(__name__)


# OK 카운트 주기 송신 간격
OK_COUNT_REPORT_INTERVAL_SEC = 5.0


class GrabItem:
    """카메라 grab 1프레임."""

    __slots__ = ("frame_id", "image", "captured_at")

    def __init__(self, frame_id: int, image: Any, captured_at: float):
        self.frame_id = frame_id
        self.image = image
        self.captured_at = captured_at


class ResultItem:
    """추론 1건 결과 (NG만 큐에 들어감)."""

    __slots__ = ("inspection_id", "result_dict", "image_bytes", "latency_ms")

    def __init__(self, inspection_id: str, result_dict: dict,
                 image_bytes: Optional[bytes], latency_ms: int):
        self.inspection_id = inspection_id
        self.result_dict = result_dict
        self.image_bytes = image_bytes
        self.latency_ms = latency_ms


class StationRunner:
    """비동기 큐 파이프라인 실행기."""

    _SENTINEL = object()

    def __init__(self,
                 config: StationConfig,
                 inferencer: BaseInferencer):
        self._config = config
        self._inferencer = inferencer
        self._tcp_client = TcpClient(config.main_server_host, config.main_server_port)
        self._tcp_client.set_station_id(config.station_id)
        self._tcp_client.set_on_model_reload(self._handle_model_reload)
        self._serial_ctrl = SerialCtrl(config.arduino_port, config.arduino_baud)

        self._grab_queue: asyncio.Queue = asyncio.Queue(maxsize=config.grab_queue_max)
        self._result_queue: asyncio.Queue = asyncio.Queue(maxsize=config.grab_queue_max)

        self._tasks: list[asyncio.Task] = []
        self._is_running = False

        self._frame_seq = 0
        self._inspection_seq = 0  # inspection_id 발급용

        # OK/NG 카운터 (주기 송신 후 reset)
        self._ok_count = 0
        self._ng_count = 0
        self._latency_sum_ms = 0
        self._latency_count = 0

        # NG 송신용 protocol_no 미리 결정
        if config.station_id == 1:
            self._ng_protocol_no = int(ProtocolNo.STATION1_NG)
        else:
            self._ng_protocol_no = int(ProtocolNo.STATION2_NG)

    # ---------- 외부 진입점 ----------

    async def run(self) -> None:
        self._is_running = True

        self._inferencer.load_model()
        self._serial_ctrl.open()

        loop = asyncio.get_running_loop()

        self._tasks.append(loop.create_task(self._run_grab_producer()))
        for i in range(self._config.inference_workers):
            self._tasks.append(loop.create_task(self._run_inference_worker(i)))
        for i in range(self._config.sender_workers):
            self._tasks.append(loop.create_task(self._run_sender_worker(i)))
        self._tasks.append(loop.create_task(self._run_ok_count_reporter()))

        try:
            await asyncio.gather(*self._tasks)
        except asyncio.CancelledError:
            logger.info("StationRunner cancelled")
        finally:
            await self._teardown()

    async def stop(self) -> None:
        self._is_running = False
        await self._grab_queue.put(self._SENTINEL)
        await self._result_queue.put(self._SENTINEL)
        for task in self._tasks:
            task.cancel()

    # ---------- Producer ----------

    async def _run_grab_producer(self) -> None:
        try:
            while self._is_running:
                # TODO: pylon grab → BGR ndarray
                dummy_image = None
                self._frame_seq += 1
                item = GrabItem(self._frame_seq, dummy_image, time.time())
                await self._grab_queue.put(item)
                await asyncio.sleep(0.5)
        except asyncio.CancelledError:
            pass

    # ---------- Inference Worker ----------

    async def _run_inference_worker(self, worker_index: int) -> None:
        loop = asyncio.get_running_loop()
        while True:
            item = await self._grab_queue.get()
            if item is self._SENTINEL:
                await self._grab_queue.put(self._SENTINEL)
                break

            try:
                t0 = time.perf_counter()
                result_dict = await loop.run_in_executor(
                    None, self._inferencer.infer, item.image
                )
                latency_ms = int((time.perf_counter() - t0) * 1000)

                inspection_id = self._issue_inspection_id()
                timestamp = self._make_iso_timestamp()

                # 통계 누적
                self._latency_sum_ms += latency_ms
                self._latency_count += 1
                is_ng = result_dict.get("result") == "NG"
                if is_ng:
                    self._ng_count += 1
                else:
                    self._ok_count += 1

                # 1006 INSPECT_META — OK/NG 공통 송신 (DB inspections 기록용)
                await self._send_inspect_meta(
                    inspection_id=inspection_id,
                    timestamp=timestamp,
                    latency_ms=latency_ms,
                    result="ng" if is_ng else "ok",
                )

                if is_ng:
                    # NG 본문 보강
                    result_dict["station_id"] = self._config.station_id
                    result_dict["timestamp"]  = timestamp
                    result_dict["latency_ms"] = latency_ms

                    image_bytes = self._encode_image(item.image)
                    await self._result_queue.put(
                        ResultItem(inspection_id, result_dict, image_bytes, latency_ms)
                    )
                    self._handle_arduino_action(result_dict)
            except Exception as exc:
                logger.exception("inference worker %d error: %s", worker_index, exc)

    # ---------- Sender Worker (NG 전용, ACK 기반) ----------

    async def _run_sender_worker(self, worker_index: int) -> None:
        while True:
            item = await self._result_queue.get()
            if item is self._SENTINEL:
                await self._result_queue.put(self._SENTINEL)
                break
            try:
                packet = PacketBuilder.build_packet(
                    protocol_no=self._ng_protocol_no,
                    body_dict=item.result_dict,
                    inspection_id=item.inspection_id,
                    image_bytes=item.image_bytes,
                )
                ok = await self._tcp_client.send_with_ack(
                    packet,
                    protocol_no=self._ng_protocol_no,
                    inspection_id=item.inspection_id,
                )
                if not ok:
                    logger.error("sender %d: NG send giveup inspection_id=%s",
                                 worker_index, item.inspection_id)
            except Exception as exc:
                logger.exception("sender worker %d error: %s", worker_index, exc)

    # ---------- OK 카운트 주기 송신 (1004) ----------

    async def _run_ok_count_reporter(self) -> None:
        try:
            while self._is_running:
                await asyncio.sleep(OK_COUNT_REPORT_INTERVAL_SEC)
                if self._latency_count == 0:
                    continue
                latency_avg = self._latency_sum_ms / self._latency_count
                body = {
                    "station_id":  self._config.station_id,
                    "ok_count":    self._ok_count,
                    "ng_count":    self._ng_count,
                    "latency_avg": round(latency_avg, 2),
                    "period":      f"{int(OK_COUNT_REPORT_INTERVAL_SEC)}s",
                }
                # 통계 reset (송신 전에 스냅샷 떠두는 게 안전하지만 단일 코루틴이라 무방)
                self._ok_count = 0
                self._ng_count = 0
                self._latency_sum_ms = 0
                self._latency_count = 0

                packet = PacketBuilder.build_packet(
                    protocol_no=int(ProtocolNo.STATION_OK_COUNT),
                    body_dict=body,
                )
                await self._tcp_client.send_fire_and_forget(packet)
        except asyncio.CancelledError:
            pass

    # ---------- INSPECT_META (1006) — OK/NG 공통 ----------

    async def _send_inspect_meta(self, inspection_id: str, timestamp: str,
                                 latency_ms: int, result: str) -> None:
        body = {
            "station_id": self._config.station_id,
            "timestamp":  timestamp,
            "latency_ms": latency_ms,
            "model_id":   0,    # TODO: 추론기에서 활성 모델 id 받아오기
            "result":     result,
        }
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.INSPECT_META),
            body_dict=body,
            inspection_id=inspection_id,
        )
        await self._tcp_client.send_fire_and_forget(packet)

    # ---------- Helpers ----------

    def _issue_inspection_id(self) -> str:
        """stationN-YYYYMMDDHHMMSSmmm-seq 형식으로 발급."""
        self._inspection_seq += 1
        ts = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")[:-3]
        return f"station{self._config.station_id}-{ts}-{self._inspection_seq:06d}"

    def _handle_model_reload(self, cmd_dict: dict) -> None:
        """MODEL_RELOAD_CMD 수신 시 추론기 모델 재로드."""
        model_path = cmd_dict.get("model_path", "")
        if model_path:
            self._config.model_path = model_path
        logger.info("Reloading inferencer model: %s", model_path)
        self._inferencer.load_model()

    def _handle_arduino_action(self, result_dict: dict) -> None:
        """NG 시 Arduino 명령 송신.
        Station1: REJECT (서보모터 리젝트 + 빨간 LED + 부저)
        Station2: ALERT:결함유형 (RGB LED + LCD 불량 유형 표시)
        """
        defect = result_dict.get("defect", "")
        if self._config.station_id == 1:
            self._serial_ctrl.send_command(f"REJECT:{defect}\n")
        else:
            defects = result_dict.get("defects", [defect])
            defect_str = ",".join(defects) if defects else defect
            self._serial_ctrl.send_command(f"ALERT:{defect_str}\n")

    @staticmethod
    def _encode_image(image: Any) -> Optional[bytes]:
        """이미지를 JPEG 바이트로 인코딩."""
        if image is None:
            return None
        try:
            import cv2
            ok, buf = cv2.imencode(".jpg", image, [cv2.IMWRITE_JPEG_QUALITY, 90])
            if ok:
                return buf.tobytes()
        except Exception:
            pass
        return None

    @staticmethod
    def _make_iso_timestamp() -> str:
        return datetime.now(timezone.utc).isoformat(timespec="milliseconds")

    async def _teardown(self) -> None:
        await self._tcp_client.close()
        self._serial_ctrl.close()
