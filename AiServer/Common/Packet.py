"""Packet.py
AI 추론 서버 -> 메인 운영 서버로 보내는 패킷 빌드 유틸.
프로토콜: [4byte length(JSON size, big-endian)] + [JSON payload] + [Image binary (NG시에만)]
"""

from __future__ import annotations

import json
import struct
from typing import Optional


class PacketBuilder:
    """검사 결과 dict를 메인 서버로 전송할 바이트 패킷으로 변환."""

    @staticmethod
    def build_packet(result_dict: dict, image_bytes: Optional[bytes] = None) -> bytes:
        """NG 시에만 호출되는 것을 전제. OK는 전송하지 않음.

        Args:
            result_dict: 추론 결과. station/result/defect/score/timestamp 등 포함.
            image_bytes: NG 이미지의 raw bytes (jpg 인코딩 권장).

        Returns:
            전송용 bytes (헤더+JSON+이미지).
        """
        payload = dict(result_dict)
        payload["image_size"] = len(image_bytes) if image_bytes else 0

        json_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        header = struct.pack(">I", len(json_bytes))  # 4byte big-endian unsigned int

        if image_bytes:
            return header + json_bytes + image_bytes
        return header + json_bytes
