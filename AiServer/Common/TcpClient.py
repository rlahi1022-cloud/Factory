"""TcpClient.py
메인 운영 서버로 결과 패킷을 송신하는 비동기 TCP 클라이언트.

주요 책임:
- 연결 끊김 시 자동 재연결.
- ACK 필수 메시지(STATION1_NG/STATION2_NG 등)에 대해 inspection_id 기준 응답 대기.
- 1초 타임아웃 내 ACK 미수신 시 최대 3회 재전송.
- 백그라운드 receiver 코루틴이 ACK 패킷을 수신해 pending future에 결과 주입.
- HEALTH_PING 수신 시 자동 HEALTH_PONG 응답.
- MODEL_RELOAD_CMD 수신 시 콜백 호출.
"""

from __future__ import annotations

import asyncio
import json
import logging
import struct
from datetime import datetime, timezone
from typing import Any, Callable, Optional

from Common.Packet import PacketBuilder
from Common.Protocol import ProtocolNo, expected_ack_no, requires_ack, PROTOCOL_VERSION


logger = logging.getLogger(__name__)


# ACK 타임아웃 / 재시도 정책 (요구사항: 1초 / 3회)
ACK_TIMEOUT_SEC = 1.0
MAX_SEND_ATTEMPTS = 3


class TcpClient:
    """비동기 TCP 송수신 클라이언트."""

    def __init__(self, host: str, port: int, reconnect_delay_sec: float = 2.0):
        self._host = host
        self._port = port
        self._reconnect_delay_sec = reconnect_delay_sec
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._send_lock = asyncio.Lock()

        # inspection_id -> Future[dict] : ACK 대기 맵
        self._pending_acks: dict[str, asyncio.Future] = {}
        self._receiver_task: Optional[asyncio.Task] = None

        # 수신 명령 콜백
        self._on_model_reload: Optional[Callable[[dict], Any]] = None
        self._station_id: int = 0

    def set_station_id(self, station_id: int) -> None:
        self._station_id = station_id

    def set_on_model_reload(self, callback: Callable[[dict], Any]) -> None:
        """MODEL_RELOAD_CMD 수신 시 호출될 콜백 등록."""
        self._on_model_reload = callback

    # ---------- 연결 관리 ----------

    async def connect(self) -> None:
        self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        logger.info("TcpClient connected to %s:%d", self._host, self._port)
        # receiver 코루틴 시작
        if self._receiver_task is None or self._receiver_task.done():
            self._receiver_task = asyncio.create_task(self._run_receiver())

    async def ensure_connected(self) -> None:
        if self._writer is not None and not self._writer.is_closing():
            return
        while True:
            try:
                await self.connect()
                return
            except OSError as exc:
                logger.warning("connect failed: %s — retry in %.1fs",
                               exc, self._reconnect_delay_sec)
                await asyncio.sleep(self._reconnect_delay_sec)

    async def close(self) -> None:
        await self._discard_writer()
        if self._receiver_task is not None:
            self._receiver_task.cancel()
            try:
                await self._receiver_task
            except asyncio.CancelledError:
                pass

    async def _discard_writer(self) -> None:
        if self._writer is None:
            return
        try:
            self._writer.close()
            await self._writer.wait_closed()
        except Exception:
            pass
        self._writer = None
        self._reader = None

    # ---------- 송신 ----------

    async def send_with_ack(self,
                            packet_bytes: bytes,
                            protocol_no: int,
                            inspection_id: str) -> bool:
        """ACK 필수 메시지 송신 (재전송 포함). 성공 시 True."""
        if not requires_ack(protocol_no):
            return await self._send_raw(packet_bytes)

        loop = asyncio.get_running_loop()
        for attempt in range(1, MAX_SEND_ATTEMPTS + 1):
            future: asyncio.Future = loop.create_future()
            self._pending_acks[inspection_id] = future

            ok = await self._send_raw(packet_bytes)
            if not ok:
                self._pending_acks.pop(inspection_id, None)
                await asyncio.sleep(self._reconnect_delay_sec)
                continue

            try:
                ack_dict = await asyncio.wait_for(future, timeout=ACK_TIMEOUT_SEC)
            except asyncio.TimeoutError:
                self._pending_acks.pop(inspection_id, None)
                logger.warning("ACK timeout inspection_id=%s attempt=%d/%d",
                               inspection_id, attempt, MAX_SEND_ATTEMPTS)
                continue

            if ack_dict.get("ack") is True:
                logger.debug("ACK ok inspection_id=%s", inspection_id)
                return True

            # NACK 수신 — 재시도 의미 없음. drop.
            logger.error("NACK received inspection_id=%s err=%s",
                         inspection_id, ack_dict.get("error_message"))
            return False

        logger.error("send_with_ack giveup inspection_id=%s after %d attempts",
                     inspection_id, MAX_SEND_ATTEMPTS)
        return False

    async def send_fire_and_forget(self, packet_bytes: bytes) -> bool:
        """ACK 미사용 메시지 (OK 카운트, 메타 등) 단순 송신."""
        return await self._send_raw(packet_bytes)

    async def _send_raw(self, packet_bytes: bytes) -> bool:
        async with self._send_lock:
            try:
                await self.ensure_connected()
                assert self._writer is not None
                self._writer.write(packet_bytes)
                await self._writer.drain()
                return True
            except (OSError, ConnectionError) as exc:
                logger.error("send failed: %s", exc)
                await self._discard_writer()
                return False

    # ---------- 수신 (ACK 라우팅 + 명령 처리) ----------

    async def _run_receiver(self) -> None:
        """ACK 패킷을 수신해 pending future에 결과를 주입.
        HEALTH_PING 수신 시 HEALTH_PONG 자동 응답.
        MODEL_RELOAD_CMD 수신 시 콜백 호출.
        """
        try:
            while True:
                if self._reader is None:
                    await asyncio.sleep(0.2)
                    continue
                try:
                    header = await self._reader.readexactly(4)
                except asyncio.IncompleteReadError:
                    logger.warning("receiver: connection closed")
                    await self._discard_writer()
                    await asyncio.sleep(self._reconnect_delay_sec)
                    continue

                json_size = struct.unpack(">I", header)[0]
                body = await self._reader.readexactly(json_size)
                try:
                    msg_dict = json.loads(body.decode("utf-8"))
                except json.JSONDecodeError as exc:
                    logger.error("receiver: invalid JSON: %s", exc)
                    continue

                # 이미지 데이터 소비 (있으면)
                image_size = int(msg_dict.get("image_size", 0))
                if image_size > 0:
                    await self._reader.readexactly(image_size)

                protocol_no = msg_dict.get("protocol_no", 0)

                # ── HEALTH_PING(1200) → HEALTH_PONG(1201) 자동 응답 ──
                if protocol_no == ProtocolNo.HEALTH_PING:
                    await self._handle_health_ping(msg_dict)
                    continue

                # ── MODEL_RELOAD_CMD(1010) → 콜백 + 응답 ──
                if protocol_no == ProtocolNo.MODEL_RELOAD_CMD:
                    await self._handle_model_reload(msg_dict)
                    continue

                # ── ACK/NACK 라우팅 ──
                inspection_id = msg_dict.get("inspection_id")
                if inspection_id and inspection_id in self._pending_acks:
                    fut = self._pending_acks.pop(inspection_id)
                    if not fut.done():
                        fut.set_result(msg_dict)
                else:
                    logger.debug("receiver: unmatched packet no=%s", protocol_no)

        except asyncio.CancelledError:
            raise
        except Exception as exc:
            logger.exception("receiver crashed: %s", exc)

    async def _handle_health_ping(self, ping_dict: dict) -> None:
        """HEALTH_PING 수신 → HEALTH_PONG 응답."""
        pong_body = {
            "station_id": self._station_id,
            "timestamp": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "status": "normal",
            "queue_size": 0,
        }
        pong_packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.HEALTH_PONG),
            body_dict=pong_body,
        )
        await self._send_raw(pong_packet)
        logger.debug("HEALTH_PONG sent")

    async def _handle_model_reload(self, cmd_dict: dict) -> None:
        """MODEL_RELOAD_CMD 수신 → 모델 리로드 콜백 + 응답."""
        request_id = cmd_dict.get("request_id", "")
        model_path = cmd_dict.get("model_path", "")
        version = cmd_dict.get("version", "")

        success = False
        if self._on_model_reload is not None:
            try:
                self._on_model_reload(cmd_dict)
                success = True
                logger.info("Model reload success: path=%s version=%s", model_path, version)
            except Exception as exc:
                logger.error("Model reload failed: %s", exc)
        else:
            logger.warning("No model reload callback registered")

        # MODEL_RELOAD_RES(1011) 응답
        res_body = {
            "station_id": self._station_id,
            "success": success,
            "version": version,
        }
        res_packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.MODEL_RELOAD_RES),
            body_dict=res_body,
            request_id=request_id,
        )
        await self._send_raw(res_packet)
