"""TrainingMain.py
AI 학습 서버 진입점.

역할:
  - TCP 서버로 운용서버의 명령 수신
  - TRAIN_START_REQ(1100) → 학습 시작
  - TRAIN_PROGRESS(1102) → 주기 전송
  - TRAIN_COMPLETE(1104) / TRAIN_FAIL(1106) → 완료/실패 전송
  - HEALTH_PING(1200) → HEALTH_PONG(1201) 응답

실행:
  cd Factory/AiServer
  python -m Training.TrainingMain
"""

from __future__ import annotations

import asyncio
import json
import logging
import signal
import struct
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from Common.Packet import PacketBuilder
from Common.Protocol import ProtocolNo, PROTOCOL_VERSION
from Training.TrainingConfig import TrainingConfig
from Training.TrainPatchcore import PatchcoreTrainer, augment_dataset
from Training.TrainYolo import YoloTrainer, create_data_yaml


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)


class TrainingServer:
    """AI 학습 서버 — 운용서버 명령 수신 + 학습 수행."""

    def __init__(self, config: TrainingConfig):
        self._config = config
        self._is_running = False
        self._training_lock = asyncio.Lock()
        self._current_training: Optional[str] = None  # 현재 학습 중인 작업 ID

        # 운용서버로 알림 전송용 TCP 클라이언트 (writer)
        self._notify_writer: Optional[asyncio.StreamWriter] = None
        self._notify_reader: Optional[asyncio.StreamReader] = None

    async def run(self) -> None:
        """TCP 서버 시작."""
        self._is_running = True

        # 운용서버 연결 (알림 전송용)
        asyncio.create_task(self._connect_to_main_server())

        server = await asyncio.start_server(
            self._handle_client,
            self._config.listen_host,
            self._config.listen_port,
        )
        addr = server.sockets[0].getsockname()
        logger.info("Training Server listening on %s:%d", addr[0], addr[1])

        async with server:
            await server.serve_forever()

    async def _connect_to_main_server(self) -> None:
        """운용서버에 TCP 연결 (학습 진행/완료 알림 전송용)."""
        while self._is_running:
            try:
                self._notify_reader, self._notify_writer = await asyncio.open_connection(
                    self._config.main_server_host,
                    self._config.main_server_port,
                )
                logger.info("Connected to main server %s:%d for notifications",
                            self._config.main_server_host, self._config.main_server_port)
                return
            except OSError as exc:
                logger.warning("Main server connection failed: %s — retry in 5s", exc)
                await asyncio.sleep(5.0)

    async def _send_to_main(self, packet: bytes) -> bool:
        """운용서버로 패킷 전송."""
        if self._notify_writer is None or self._notify_writer.is_closing():
            await self._connect_to_main_server()
        try:
            if self._notify_writer is not None:
                self._notify_writer.write(packet)
                await self._notify_writer.drain()
                return True
        except (OSError, ConnectionError) as exc:
            logger.error("Send to main server failed: %s", exc)
            self._notify_writer = None
        return False

    # ── TCP 클라이언트 핸들러 ──

    async def _handle_client(self, reader: asyncio.StreamReader,
                             writer: asyncio.StreamWriter) -> None:
        """운용서버로부터 수신한 명령 처리."""
        addr = writer.get_extra_info("peername")
        logger.info("Client connected: %s", addr)

        try:
            while True:
                # 4바이트 헤더 읽기
                header = await reader.readexactly(4)
                json_size = struct.unpack(">I", header)[0]

                # JSON 본문 읽기
                body = await reader.readexactly(json_size)
                msg = json.loads(body.decode("utf-8"))

                # 이미지 데이터 소비
                image_size = int(msg.get("image_size", 0))
                if image_size > 0:
                    await reader.readexactly(image_size)

                protocol_no = msg.get("protocol_no", 0)

                # 메시지 분기
                if protocol_no == ProtocolNo.HEALTH_PING:
                    await self._handle_health_ping(writer, msg)
                elif protocol_no == ProtocolNo.TRAIN_START_REQ:
                    await self._handle_train_start(writer, msg)
                else:
                    logger.debug("Unknown protocol_no: %d", protocol_no)

        except asyncio.IncompleteReadError:
            logger.info("Client disconnected: %s", addr)
        except Exception as exc:
            logger.exception("Client handler error: %s", exc)
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

    # ── HEALTH_PING 응답 ──

    async def _handle_health_ping(self, writer: asyncio.StreamWriter,
                                  ping_msg: dict) -> None:
        pong_body = {
            "server_type": "training",
            "timestamp": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "status": "training" if self._current_training else "idle",
            "current_task": self._current_training or "",
        }
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.HEALTH_PONG),
            body_dict=pong_body,
        )
        writer.write(packet)
        await writer.drain()

    # ── 학습 시작 명령 ──

    async def _handle_train_start(self, writer: asyncio.StreamWriter,
                                  msg: dict) -> None:
        """TRAIN_START_REQ(1100) 처리."""
        request_id = msg.get("request_id", "")
        station_id = msg.get("station_id", 1)
        model_type = msg.get("model_type", "PatchCore")
        data_path = msg.get("data_path", "")

        # 이미 학습 중이면 거부
        if self._current_training is not None:
            res_body = {
                "success": False,
                "message": f"Already training: {self._current_training}",
            }
            res_packet = PacketBuilder.build_packet(
                protocol_no=int(ProtocolNo.TRAIN_START_RES),
                body_dict=res_body,
                request_id=request_id,
            )
            writer.write(res_packet)
            await writer.drain()
            return

        # 수락 응답
        res_body = {"success": True, "message": "Training accepted"}
        res_packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_START_RES),
            body_dict=res_body,
            request_id=request_id,
        )
        writer.write(res_packet)
        await writer.drain()

        # 비동기로 학습 시작
        task_id = f"station{station_id}_{model_type}_{request_id}"
        asyncio.create_task(
            self._run_training(task_id, station_id, model_type, data_path, request_id)
        )

    async def _run_training(self, task_id: str, station_id: int,
                            model_type: str, data_path: str,
                            request_id: str) -> None:
        """학습 실행 (백그라운드 태스크)."""
        self._current_training = task_id
        logger.info("Training started: %s (station=%d, type=%s)", task_id, station_id, model_type)

        loop = asyncio.get_running_loop()

        def progress_callback(info: dict) -> None:
            """학습 진행 알림을 운용서버로 전송."""
            asyncio.run_coroutine_threadsafe(
                self._send_progress(info, request_id), loop
            )

        try:
            if model_type.upper() == "YOLO11":
                result = await loop.run_in_executor(
                    None,
                    self._train_yolo,
                    station_id, data_path, progress_callback,
                )
            else:
                result = await loop.run_in_executor(
                    None,
                    self._train_patchcore,
                    station_id, data_path, progress_callback,
                )

            if result["success"]:
                await self._send_train_complete(result, request_id)
            else:
                await self._send_train_fail(result, request_id)

        except Exception as exc:
            logger.exception("Training task error: %s", exc)
            await self._send_train_fail(
                {"message": str(exc), "version": "", "model_path": ""},
                request_id,
            )
        finally:
            self._current_training = None
            logger.info("Training finished: %s", task_id)

    # ── 학습 실행 함수 (executor에서 동기 실행) ──

    def _train_patchcore(self, station_id: int, data_path: str,
                         progress_callback) -> dict:
        """PatchCore 학습."""
        data_dir = data_path or str(Path(self._config.data_root) / f"station{station_id}" / "normal")

        # 데이터 증강
        try:
            augment_dataset(data_dir, factor=self._config.augmentation_factor)
        except Exception as exc:
            logger.warning("Augmentation skipped: %s", exc)

        trainer = PatchcoreTrainer(
            station_id=station_id,
            data_dir=data_dir,
            output_dir=self._config.model_output_dir,
            backbone=self._config.patchcore_backbone,
            input_size=self._config.patchcore_input_size,
            batch_size=self._config.patchcore_batch_size,
            num_workers=self._config.patchcore_num_workers,
            device=self._config.device,
            progress_callback=progress_callback,
        )
        return trainer.train()

    def _train_yolo(self, station_id: int, data_path: str,
                    progress_callback) -> dict:
        """YOLO11 학습."""
        data_dir = data_path or str(Path(self._config.data_root) / f"station{station_id}" / "yolo")
        data_yaml = str(Path(data_dir) / "data.yaml")

        if not Path(data_yaml).exists():
            data_yaml = create_data_yaml(data_dir, data_yaml)

        trainer = YoloTrainer(
            data_yaml=data_yaml,
            output_dir=self._config.model_output_dir,
            base_model=self._config.yolo_base_model,
            input_size=self._config.yolo_input_size,
            epochs=self._config.yolo_epochs,
            batch_size=self._config.yolo_batch_size,
            patience=self._config.yolo_patience,
            device=self._config.device,
            progress_callback=progress_callback,
        )
        return trainer.train()

    # ── 운용서버 알림 전송 ──

    async def _send_progress(self, info: dict, request_id: str) -> None:
        """TRAIN_PROGRESS(1102) 전송."""
        body = {
            "station_id": info.get("station_id", 0),
            "model_type": info.get("model_type", ""),
            "progress": info.get("progress", 0),
            "epoch": info.get("epoch", 0),
            "loss": info.get("loss", 0.0),
            "status": info.get("status", ""),
        }
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_PROGRESS),
            body_dict=body,
            request_id=request_id,
        )
        await self._send_to_main(packet)

    async def _send_train_complete(self, result: dict, request_id: str) -> None:
        """TRAIN_COMPLETE(1104) 전송."""
        body = {
            "model_path": result.get("model_path", ""),
            "version": result.get("version", ""),
            "accuracy": result.get("accuracy", 0.0),
            "message": result.get("message", ""),
        }
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_COMPLETE),
            body_dict=body,
            request_id=request_id,
        )
        await self._send_to_main(packet)
        logger.info("TRAIN_COMPLETE sent: %s", result.get("message"))

    async def _send_train_fail(self, result: dict, request_id: str) -> None:
        """TRAIN_FAIL(1106) 전송."""
        body = {
            "error_code": "TRAIN_ERROR",
            "message": result.get("message", "Unknown error"),
            "version": result.get("version", ""),
        }
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_FAIL),
            body_dict=body,
            request_id=request_id,
        )
        await self._send_to_main(packet)
        logger.error("TRAIN_FAIL sent: %s", result.get("message"))


# ── 진입점 ──

async def main() -> None:
    config = TrainingConfig(
        listen_host="0.0.0.0",
        listen_port=9100,
        main_server_host="127.0.0.1",
        main_server_port=9000,
        device="cuda",
        data_root="./data",
        model_output_dir="./models",
    )

    server = TrainingServer(config)

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def request_stop() -> None:
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, request_stop)
        except NotImplementedError:
            pass

    server_task = loop.create_task(server.run())

    try:
        await stop_event.wait()
    except asyncio.CancelledError:
        pass

    server_task.cancel()
    try:
        await server_task
    except asyncio.CancelledError:
        pass


if __name__ == "__main__":
    asyncio.run(main())
