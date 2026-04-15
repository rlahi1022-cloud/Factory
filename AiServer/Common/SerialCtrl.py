"""SerialCtrl.py
Arduino 시리얼 제어 (골격). 실제 사용 시 pyserial 의존.
"""

from __future__ import annotations

import logging
from typing import Optional


logger = logging.getLogger(__name__)


class SerialCtrl:
    """Arduino 시리얼 명령 송신기."""

    def __init__(self, port: Optional[str], baud: int = 9600):
        self._port = port
        self._baud = baud
        self._serial = None  # pyserial.Serial 인스턴스 (실제 환경에서 주입)

    def open(self) -> None:
        if self._port is None:
            logger.info("SerialCtrl: port not configured, skip open")
            return
        # TODO: import serial; self._serial = serial.Serial(self._port, self._baud, timeout=1)
        logger.info("SerialCtrl open %s @ %d", self._port, self._baud)

    def close(self) -> None:
        if self._serial is not None:
            # self._serial.close()
            self._serial = None

    def send_command(self, command: str) -> None:
        """단일 명령 송신. 예: 'REJECT\n', 'ALERT:CAP\n'."""
        if self._serial is None:
            logger.debug("SerialCtrl noop send: %s", command.strip())
            return
        # self._serial.write(command.encode("ascii"))
