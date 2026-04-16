"""SerialCtrl.py — Arduino 시리얼 통신 제어

이 파일은 AI 추론 서버가 Arduino에 시리얼(USB) 명령을 보내는 기능을 제공한다.

Station1 (입고검사): NG 시 → "REJECT:결함유형\n" 전송 → 서보모터 리젝트 + 빨간 LED + 부저
Station2 (조립검사): NG 시 → "ALERT:결함목록\n" 전송 → RGB LED + LCD 불량 유형 표시

실제 사용 시 pyserial 패키지가 필요하다: pip install pyserial
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리

import logging  # 로그 출력용 표준 라이브러리
from typing import Optional  # Optional: 값이 None일 수 있음


# 이 모듈 전용 로거 생성 — 로그 메시지에 모듈 이름이 표시된다
logger = logging.getLogger(__name__)


class SerialCtrl:
    """Arduino 시리얼 명령 송신기.

    사용 흐름:
        1. SerialCtrl("COM3", 9600) — 객체 생성 (아직 연결 안 됨)
        2. .open() — 시리얼 포트 열기
        3. .send_command("REJECT:crack\n") — 명령 전송
        4. .close() — 시리얼 포트 닫기
    """

    def __init__(self, port: Optional[str], baud: int = 9600):
        """시리얼 컨트롤러 초기화.

        Args:
            port: 시리얼 포트 이름 (예: "COM3", "/dev/ttyUSB0")
                  None이면 Arduino를 사용하지 않는 것으로 간주.
            baud: 통신 속도 (기본 9600bps, Arduino 기본값과 일치)
        """
        self._port = port    # 시리얼 포트 이름
        self._baud = baud    # 통신 속도 (baud rate)
        self._serial = None  # pyserial.Serial 인스턴스 (open() 시 생성)

    def open(self) -> None:
        """시리얼 포트를 연다.

        port가 None이면 아무 것도 하지 않는다 (Arduino 미사용 모드).
        실제 환경에서는 pyserial의 Serial 객체를 생성한다.
        """
        if self._port is None:
            # 포트가 설정되지 않았으면 Arduino를 사용하지 않는다
            logger.info("SerialCtrl: port not configured, skip open")
            return
        try:
            import serial  # pyserial 패키지 — pip install pyserial

            # 시리얼 포트 열기: timeout=1초 (응답 대기 최대 1초)
            self._serial = serial.Serial(self._port, self._baud, timeout=1)
            logger.info("SerialCtrl opened %s @ %d bps", self._port, self._baud)
        except ImportError:
            # pyserial이 설치되지 않은 환경 (개발/테스트 시)
            logger.warning("pyserial not installed — SerialCtrl in dummy mode")
        except Exception as exc:
            # 포트 열기 실패 (포트 없음, 권한 문제 등)
            logger.error("SerialCtrl open failed: %s", exc)

    def close(self) -> None:
        """시리얼 포트를 닫는다. 이미 닫혀있으면 아무 것도 안 한다."""
        if self._serial is not None:
            try:
                self._serial.close()  # 포트 닫기
            except Exception:
                pass  # 닫기 실패해도 무시 (이미 닫혀있을 수 있음)
            self._serial = None  # 참조 해제

    def send_command(self, command: str) -> None:
        """Arduino에 단일 명령을 전송한다.

        Args:
            command: 전송할 명령 문자열
                Station1 예: "REJECT:crack\n"
                Station2 예: "ALERT:cap_missing,label_tilt\n"

        시리얼이 연결되지 않은 상태면 로그만 출력하고 넘어간다.
        """
        if self._serial is None:
            # 시리얼 미연결 — 디버그 로그만 출력 (개발/테스트 환경)
            logger.debug("SerialCtrl noop send: %s", command.strip())
            return
        try:
            # 문자열을 ASCII 바이트로 변환하여 전송
            self._serial.write(command.encode("ascii"))
        except Exception as exc:
            logger.error("SerialCtrl send failed: %s", exc)
