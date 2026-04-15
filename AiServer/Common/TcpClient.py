"""TcpClient.py
메인 운영 서버로 결과 패킷을 송신하는 비동기 TCP 클라이언트.

주요 책임:
- 연결 끊김 시 자동 재연결.
- ACK 필수 메시지(STATION1_NG/STATION2_NG 등)에 대해 inspection_id 기준 응답 대기.
- 1초 타임아웃 내 ACK 미수신 시 최대 3회 재전송.
- 백그라운드 receiver 코루틴이 ACK 패킷을 수신해 pending future에 결과 주입.
"""

from __future__ import annotations

import asyncio
import json
import logging
import struct
from typing import Optional

from Common.Protocol import ProtocolNo, expected_ack_no, requires_ack


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

    # ---------- 수신 (ACK 라우팅) ----------

    async def _run_receiver(self) -> None:
        """ACK 패킷을 수신해 pending future에 결과를 주입."""
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
                    ack_dict = json.loads(body.decode("utf-8"))
                except json.JSONDecodeError as exc:
                    logger.error("receiver: invalid JSON: %s", exc)
                    continue

                # 이미지 없는 ACK라고 가정 (image_size==0).
                # 만약 image_size > 0 응답이 온다면 추가로 읽고 폐기.
                image_size = int(ack_dict.get("image_size", 0))
                if image_size > 0:
                    await self._reader.readexactly(image_size)

                inspection_id = ack_dict.get("inspection_id")
                if inspection_id and inspection_id in self._pending_acks:
                    fut = self._pending_acks.pop(inspection_id)
                    if not fut.done():
                        fut.set_result(ack_dict)
                else:
                    # 모델 리로드 명령 등 비-ACK 수신 분기 — 추후 핸들러
                    logger.debug("receiver: unmatched packet no=%s",
                                 ack_dict.get("protocol_no"))
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            logger.exception("receiver crashed: %s", exc)
