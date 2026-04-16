"""Packet.py — TCP 패킷 빌드/파싱 유틸리티

이 파일은 AI 추론 서버가 운용 서버(메인 서버)로 보내는 TCP 패킷을
만들고(build) 해석하는(parse) 기능을 제공한다.

패킷 구조 (와이어 포맷):
  [4바이트 헤더: JSON 크기(big-endian)] + [JSON 본문] + [이미지 바이너리 (NG일 때만)]

예시:
  헤더: 00 00 00 7B (123바이트)
  JSON: {"protocol_no": 1000, "station_id": 1, "result": "NG", ...}
  이미지: (JPEG 바이너리 데이터)
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리

import json     # JSON 인코딩/디코딩 라이브러리
import struct   # 바이너리 데이터 패킹/언패킹 (4바이트 헤더 만들 때 사용)
from typing import Optional  # Optional: 값이 None일 수 있음을 표현

from Common.Protocol import PROTOCOL_VERSION  # 프로토콜 버전 문자열 ("1.0")


class PacketBuilder:
    """검사 결과 dict를 메인 서버로 전송할 바이트 패킷으로 변환하는 클래스.

    사용 예:
        packet = PacketBuilder.build_packet(
            protocol_no=1000,
            body_dict={"station_id": 1, "result": "NG", "score": 0.87},
            inspection_id="station1-20260416-000001",
            image_bytes=jpeg_bytes,
        )
        # → bytes 객체가 반환됨 → TCP 소켓으로 전송
    """

    @staticmethod  # 인스턴스 없이 클래스에서 바로 호출 가능한 메서드
    def build_packet(protocol_no: int,
                     body_dict: dict,
                     inspection_id: Optional[str] = None,
                     request_id: Optional[str] = None,
                     image_bytes: Optional[bytes] = None) -> bytes:
        """패킷을 빌드한다. 공통 헤더 필드를 자동으로 추가한다.

        Args:
            protocol_no: 메시지 번호 (예: 1000=STATION1_NG)
            body_dict: 메시지별 본문 필드 (station_id, result, score 등)
            inspection_id: 검사 ID (NG 결과 전송 시 필수, OK카운트 등에서는 생략)
            request_id: 요청/응답 매칭용 ID (있으면 넣고, 없으면 생략)
            image_bytes: NG 이미지의 JPEG 바이트 (정상이면 None)

        Returns:
            전송할 바이트열 = [4바이트 헤더] + [JSON] + [이미지(있으면)]
        """
        # 본문 dict를 복사해서 공통 필드를 추가한다 (원본 변경 방지)
        payload = dict(body_dict)

        # 공통 필드 자동 주입
        payload["protocol_no"]      = int(protocol_no)       # 메시지 번호 (정수)
        payload["protocol_version"] = PROTOCOL_VERSION        # 프로토콜 버전 ("1.0")

        # 검사 ID — NG 결과 계열에서는 필수이므로 값이 있으면 추가
        if inspection_id is not None:
            payload["inspection_id"] = inspection_id

        # 요청/응답 매칭 ID — 있으면 추가
        if request_id is not None:
            payload["request_id"] = request_id

        # 이미지 크기 — 수신 측에서 이 값을 보고 이미지를 추가로 읽을지 결정
        payload["image_size"] = len(image_bytes) if image_bytes else 0

        # JSON을 UTF-8 바이트로 인코딩 (한국어 등을 그대로 보존하기 위해 ensure_ascii=False)
        json_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")

        # 헤더: JSON의 바이트 크기를 4바이트 big-endian 정수로 패킹
        # ">I" = big-endian unsigned int (4바이트)
        # 예: JSON이 123바이트면 → b'\x00\x00\x00\x7b'
        header = struct.pack(">I", len(json_bytes))

        # 최종 패킷 조립: 헤더 + JSON + 이미지(있으면)
        if image_bytes:
            return header + json_bytes + image_bytes  # NG: 이미지 포함
        return header + json_bytes                     # OK 또는 이미지 없는 메시지

    @staticmethod
    def parse_json_only(raw_json_bytes: bytes) -> dict:
        """이미지가 없는 응답(ACK 등)의 JSON 부분만 파싱한다.

        Args:
            raw_json_bytes: JSON 부분의 바이트열
        Returns:
            파싱된 dict (예: {"protocol_no": 1001, "ack": true, ...})
        """
        return json.loads(raw_json_bytes.decode("utf-8"))
