"""Packet.py
AI 추론 서버 -> 메인 운영 서버로 보내는 패킷 빌드/파싱 유틸.

프로토콜:
  [4byte length(JSON size, big-endian)] + [JSON payload] + [Image binary (있을 때만)]

JSON 본문 공통 필드 (요구사항 분석서 "공통 패킷 구조" 기준):
  - protocol_no       : int     (필수)
  - protocol_version  : str     (필수)
  - inspection_id     : str     (검사 결과 계열 필수)
  - request_id        : str     (optional)
  - station_id        : int
  - timestamp         : str     (ISO8601)
  - image_size        : int     (NG 이미지 동봉 시 양수)
"""

from __future__ import annotations

import json
import struct
from typing import Optional

from Common.Protocol import PROTOCOL_VERSION


class PacketBuilder:
    """검사 결과/명령 dict를 메인 서버로 전송할 바이트 패킷으로 변환."""

    @staticmethod
    def build_packet(protocol_no: int,
                     body_dict: dict,
                     inspection_id: Optional[str] = None,
                     request_id: Optional[str] = None,
                     image_bytes: Optional[bytes] = None) -> bytes:
        """공통 헤더 필드를 자동 주입하고 패킷을 빌드한다.

        Args:
            protocol_no: 메시지 번호 (ProtocolNo enum 값).
            body_dict: 메시지별 본문 필드 (station_id/result/score 등).
            inspection_id: 검사 결과 계열에서 필수.
            request_id: 요청/응답 매칭용 (있으면 기록).
            image_bytes: NG 이미지 raw bytes (없으면 None).

        Returns:
            전송용 bytes (헤더+JSON+이미지).
        """
        payload = dict(body_dict)
        payload["protocol_no"]      = int(protocol_no)
        payload["protocol_version"] = PROTOCOL_VERSION
        if inspection_id is not None:
            payload["inspection_id"] = inspection_id
        if request_id is not None:
            payload["request_id"] = request_id
        payload["image_size"] = len(image_bytes) if image_bytes else 0

        json_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        header = struct.pack(">I", len(json_bytes))

        if image_bytes:
            return header + json_bytes + image_bytes
        return header + json_bytes

    @staticmethod
    def parse_json_only(raw_json_bytes: bytes) -> dict:
        """ACK 등 이미지 없는 응답 JSON 파싱."""
        return json.loads(raw_json_bytes.decode("utf-8"))
