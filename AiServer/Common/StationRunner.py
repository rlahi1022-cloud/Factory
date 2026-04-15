"""StationRunner.py
AI 추론 서버의 비동기 큐 파이프라인 베이스.

파이프라인:
  [Pylon Grab Producer] -> grab_queue -> [Inference Worker(s)] -> result_queue -> [Sender Worker(s)] -> 메인 서버

설계 원칙:
- 모든 단계는 asyncio.Queue로 분리 → 단계별 백프레셔/병렬도 독립 조정.
- 추론은 CPU/GPU 바운드이므로 loop.run_in_executor 로 별도 스레드에서 실행.
- 송신은 NG 시에만 수행. OK는 큐에 넣지 않거나 폐기.
- 본 골격은 모델/카메라 실제 구현은 placeholder. 인터페이스만 정의.
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
from Common.SerialCtrl import SerialCtrl
from Common.TcpClient import TcpClient


logger = logging.getLogger(__name__)


class GrabItem:
    """카메라 grab 1프레임."""

    __slots__ = ("frame_id", "image", "captured_at")

    def __init__(self, frame_id: int, image: Any, captured_at: float):
        self.frame_id = frame_id
        self.image = image
        self.captured_at = captured_at


class ResultItem:
    """추론 1건 결과."""

    __slots__ = ("frame_id", "result_dict", "image_bytes", "latency_ms")

    def __init__(self, frame_id: int, result_dict: dict,
                 image_bytes: Optional[bytes], latency_ms: int):
        self.frame_id = frame_id
        self.result_dict = result_dict
        self.image_bytes = image_bytes
        self.latency_ms = latency_ms


class StationRunner:
    """비동기 큐 파이프라인 실행기."""

    # 종료 신호용 sentinel
    _SENTINEL = object()

    def __init__(self,
                 config: StationConfig,
                 inferencer: BaseInferencer):
        self._config = config
        self._inferencer = inferencer
        self._tcp_client = TcpClient(config.main_server_host, config.main_server_port)
        self._serial_ctrl = SerialCtrl(config.arduino_port, config.arduino_baud)

        self._grab_queue: asyncio.Queue = asyncio.Queue(maxsize=config.grab_queue_max)
        self._result_queue: asyncio.Queue = asyncio.Queue(maxsize=config.grab_queue_max)

        self._tasks: list[asyncio.Task] = []
        self._is_running = False
        self._frame_seq = 0

    # ---------- 외부 진입점 ----------

    async def run(self) -> None:
        """파이프라인 실행. Ctrl+C 등 외부 종료까지 블록."""
        self._is_running = True

        self._inferencer.load_model()
        self._serial_ctrl.open()
        # 초기 연결은 굳이 시도하지 않음 (sender_worker가 ensure_connected)

        loop = asyncio.get_running_loop()

        # 1) Producer: 카메라 grab
        self._tasks.append(loop.create_task(self._run_grab_producer()))

        # 2) Inference workers
        for i in range(self._config.inference_workers):
            self._tasks.append(loop.create_task(self._run_inference_worker(i)))

        # 3) Sender workers
        for i in range(self._config.sender_workers):
            self._tasks.append(loop.create_task(self._run_sender_worker(i)))

        try:
            await asyncio.gather(*self._tasks)
        except asyncio.CancelledError:
            logger.info("StationRunner cancelled")
        finally:
            await self._teardown()

    async def stop(self) -> None:
        """우아한 종료 — sentinel 주입 후 task 취소."""
        self._is_running = False
        # Sentinel을 큐에 넣어 워커가 자연스럽게 빠져나오게
        await self._grab_queue.put(self._SENTINEL)
        await self._result_queue.put(self._SENTINEL)
        for task in self._tasks:
            task.cancel()

    # ---------- Producer ----------

    async def _run_grab_producer(self) -> None:
        """Pylon 카메라에서 프레임을 grab해 grab_queue에 적재.

        본 골격에서는 실제 카메라 대신 0.5초 주기 더미 프레임 생성.
        실제 구현 시 pypylon 사용:
          camera = pylon.InstantCamera(pylon.TlFactory.GetInstance().CreateFirstDevice())
          camera.StartGrabbing(); ...
        """
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
                # 다른 inference worker도 종료시키기 위해 다시 주입
                await self._grab_queue.put(self._SENTINEL)
                break

            try:
                t0 = time.perf_counter()
                # 추론은 별도 스레드에서 (블로킹 가능)
                result_dict = await loop.run_in_executor(
                    None, self._inferencer.infer, item.image
                )
                latency_ms = int((time.perf_counter() - t0) * 1000)

                # 메타 정보 보강
                result_dict.setdefault("station", self._config.station_id)
                result_dict.setdefault("type", "inspect")
                result_dict["timestamp"] = self._make_iso_timestamp()
                result_dict["latency_ms"] = latency_ms
                result_dict["frame_id"] = item.frame_id

                # NG 만 result_queue로 (OK는 폐기 — 필요 시 통계 카운터만 증가)
                if result_dict.get("result") == "NG":
                    image_bytes = self._encode_image(item.image)
                    await self._result_queue.put(
                        ResultItem(item.frame_id, result_dict, image_bytes, latency_ms)
                    )
                    self._handle_arduino_action(result_dict)
            except Exception as exc:
                logger.exception("inference worker %d error: %s", worker_index, exc)

    # ---------- Sender Worker ----------

    async def _run_sender_worker(self, worker_index: int) -> None:
        while True:
            item = await self._result_queue.get()
            if item is self._SENTINEL:
                await self._result_queue.put(self._SENTINEL)
                break
            try:
                packet = PacketBuilder.build_packet(item.result_dict, item.image_bytes)
                ok = await self._tcp_client.send_packet(packet)
                if not ok:
                    # 재시도 정책: 1회 한정 재투입 (간단 구현)
                    logger.warning("sender %d: requeue frame %d", worker_index, item.frame_id)
                    await asyncio.sleep(1.0)
                    await self._result_queue.put(item)
            except Exception as exc:
                logger.exception("sender worker %d error: %s", worker_index, exc)

    # ---------- Helpers ----------

    def _handle_arduino_action(self, result_dict: dict) -> None:
        """NG 시 Arduino 명령 송신. 스테이션별 로직은 서브클래스에서 override 권장."""
        defect = result_dict.get("defect", "")
        self._serial_ctrl.send_command(f"NG:{defect}\n")

    @staticmethod
    def _encode_image(image: Any) -> Optional[bytes]:
        """ndarray -> jpg bytes. 실제 구현 시 cv2.imencode 사용. 골격에서는 None."""
        # TODO: import cv2; ok, buf = cv2.imencode(".jpg", image); return buf.tobytes()
        if image is None:
            return None
        return None

    @staticmethod
    def _make_iso_timestamp() -> str:
        return datetime.now(timezone.utc).isoformat(timespec="milliseconds")

    async def _teardown(self) -> None:
        await self._tcp_client.close()
        self._serial_ctrl.close()
