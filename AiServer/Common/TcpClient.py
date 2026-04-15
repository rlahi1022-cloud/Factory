"""TcpClient.py
메인 운영 서버로 결과 패킷을 송신하는 비동기 TCP 클라이언트.
asyncio.open_connection 사용. 연결 끊김 시 자동 재연결.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Optional


logger = logging.getLogger(__name__)


class TcpClient:
    """비동기 TCP 송신 클라이언트."""

    def __init__(self, host: str, port: int, reconnect_delay_sec: float = 2.0):
        self._host = host
        self._port = port
        self._reconnect_delay_sec = reconnect_delay_sec
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._lock = asyncio.Lock()

    async def connect(self) -> None:
        """연결 (실패 시 예외 raise)."""
        self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        logger.info("TcpClient connected to %s:%d", self._host, self._port)

    async def ensure_connected(self) -> None:
        """연결되어 있지 않으면 재연결."""
        if self._writer is not None and not self._writer.is_closing():
            return
        while True:
            try:
                await self.connect()
                return
            except OSError as exc:
                logger.warning("connect failed: %s — retry in %.1fs", exc, self._reconnect_delay_sec)
                await asyncio.sleep(self._reconnect_delay_sec)

    async def send_packet(self, packet_bytes: bytes) -> bool:
        """패킷 1건 송신. 실패 시 False 반환 후 재연결을 위해 writer 폐기."""
        async with self._lock:
            try:
                await self.ensure_connected()
                assert self._writer is not None
                self._writer.write(packet_bytes)
                await self._writer.drain()
                return True
            except (OSError, ConnectionError) as exc:
                logger.error("send_packet failed: %s", exc)
                await self._discard_writer()
                return False

    async def close(self) -> None:
        await self._discard_writer()

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
