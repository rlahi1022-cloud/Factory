"""TestPipeline.py — AI 추론서버 역할 시뮬레이션 (numpy/torch 없이 순수 TCP)

실제 Station1/Station2 추론서버가 있다면 카메라 → 추론 → 전송 파이프라인을
거치지만, 이 스크립트는 **가짜 NG 결과**를 직접 만들어 MainServer로 전송한다.

목적:
  1. MainServer TCP 수신 정상 동작 확인 (port 9000)
  2. 프로토콜 파싱 (4byte 헤더 + JSON + 이미지 바이너리)
  3. DB INSERT (inspections/assemblies 테이블)
  4. ACK 회신 흐름
  5. OK 카운트 fire-and-forget
  6. INSPECT_META fire-and-forget

사용법:
  cd /home/lms/Desktop/Factory/AiServer
  python3 tests/TestPipeline.py

  # 옵션: 전송 개수
  python3 tests/TestPipeline.py --count 10

  # 옵션: 스테이션 (1 또는 2)
  python3 tests/TestPipeline.py --station 2
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


# ── 설정 (config.json에서 로드) ──
_THIS = Path(__file__).resolve()
_CONFIG = _THIS.parent.parent.parent / "config" / "config.json"

with open(_CONFIG, "r", encoding="utf-8") as f:
    _cfg = json.load(f)

MAIN_HOST = _cfg["network"]["main_server_host"]
MAIN_PORT = _cfg["network"]["main_server_ai_port"]


# ── 프로토콜 번호 (Protocol.py에서 가져와도 됨) ──
STATION1_NG         = 1000
STATION2_NG         = 1002
STATION_OK_COUNT    = 1004
INSPECT_META        = 1006
STATION1_NG_ACK     = 1001
STATION2_NG_ACK     = 1003


# ── 가짜 JPEG 이미지 (최소 유효 JPEG 헤더 + 더미 데이터) ──
FAKE_JPEG = bytes([
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
]) + bytes(1024)  # 1KB 더미


# ── 유틸리티 ──
def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _build_packet(body_dict: dict, image_bytes: bytes = b"") -> bytes:
    """패킷 빌드: [4byte BE length] + [JSON UTF-8] + [image]"""
    body_dict["protocol_version"] = "1.0"
    body_dict["image_size"] = len(image_bytes)
    json_str = json.dumps(body_dict, ensure_ascii=False)
    json_bytes = json_str.encode("utf-8")
    header = struct.pack(">I", len(json_bytes))
    return header + json_bytes + image_bytes


def _recv_exactly(sock: socket.socket, n: int, timeout: float = 2.0) -> bytes:
    """정확히 n바이트 수신 (타임아웃 지원)"""
    sock.settimeout(timeout)
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("연결 끊김")
        data += chunk
    return data


def _recv_one_packet(sock: socket.socket, timeout: float = 2.0) -> dict:
    """서버로부터 패킷 1개 수신 (주로 ACK)"""
    header = _recv_exactly(sock, 4, timeout)
    json_size = struct.unpack(">I", header)[0]
    if json_size == 0 or json_size > 64 * 1024:
        raise ValueError(f"비정상 JSON 크기: {json_size}")
    body = _recv_exactly(sock, json_size, timeout)
    msg = json.loads(body.decode("utf-8"))
    # 이미지 바이너리 수신 (있으면)
    img_size = int(msg.get("image_size", 0))
    if img_size > 0:
        _recv_exactly(sock, img_size, timeout)
    return msg


# ── 테스트 시나리오 ──
def test_ng_packet(sock: socket.socket, station_id: int, seq: int) -> bool:
    """NG 패킷 1건 전송 후 ACK 확인"""
    ts = _now_iso()
    # inspection_id 형식: "stationN-YYYYMMDDHHMMSSmmm-NNNNNN"
    dt_str = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")[:-3]
    inspection_id = f"station{station_id}-{dt_str}-{seq:06d}"

    proto_no = STATION1_NG if station_id == 1 else STATION2_NG
    ack_no   = STATION1_NG_ACK if station_id == 1 else STATION2_NG_ACK

    body = {
        "protocol_no": proto_no,
        "inspection_id": inspection_id,
        "station_id": station_id,
        "result": "ng",
        "defect_type": "anomaly" if station_id == 1 else "cap_loose",
        "score": 0.87,
        "latency_ms": 45,
        "timestamp": ts,
    }

    # Station2는 추가 필드
    if station_id == 2:
        body.update({
            "cap_ok": 0,
            "label_ok": 1,
            "fill_ok": 1,
            "patchcore_score": 0.72,
            "detections": [
                {"class": "cap_loose", "confidence": 0.87, "bbox": [100, 150, 200, 250]}
            ],
        })

    packet = _build_packet(body, FAKE_JPEG)

    print(f"  [TX] NG station={station_id} id={inspection_id} ({len(packet)} bytes)")
    sock.sendall(packet)

    # ACK 수신 대기
    try:
        ack = _recv_one_packet(sock, timeout=3.0)
        if ack.get("protocol_no") == ack_no and ack.get("ack") is True:
            print(f"  [RX] ACK OK id={ack.get('inspection_id')}")
            return True
        else:
            print(f"  [RX] NACK? {ack}")
            return False
    except Exception as e:
        print(f"  [ERR] ACK 수신 실패: {e}")
        return False


def test_ok_count(sock: socket.socket, station_id: int) -> None:
    """OK 카운트 fire-and-forget 전송 (ACK 없음)"""
    body = {
        "protocol_no": STATION_OK_COUNT,
        "station_id": station_id,
        "ok_count": 152,
        "ng_count": 3,
        "latency_avg": 42.5,
        "period": "5s",
        "timestamp": _now_iso(),
    }
    packet = _build_packet(body)
    print(f"  [TX] OK_COUNT station={station_id} ok=152 ng=3 (fire-and-forget)")
    sock.sendall(packet)


def test_inspect_meta(sock: socket.socket, station_id: int) -> None:
    """검사 메타데이터 (OK 건) 전송"""
    dt_str = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")[:-3]
    body = {
        "protocol_no": INSPECT_META,
        "inspection_id": f"station{station_id}-{dt_str}-meta",
        "station_id": station_id,
        "result": "ok",
        "score": 0.05,
        "latency_ms": 38,
        "timestamp": _now_iso(),
    }
    packet = _build_packet(body)
    print(f"  [TX] INSPECT_META station={station_id} result=ok (fire-and-forget)")
    sock.sendall(packet)


# ── 메인 ──
def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--station", type=int, default=1, choices=[1, 2],
                        help="스테이션 ID (1 또는 2)")
    parser.add_argument("--count", type=int, default=3,
                        help="NG 전송 개수")
    parser.add_argument("--host", default=MAIN_HOST,
                        help=f"MainServer IP (기본: {MAIN_HOST})")
    parser.add_argument("--port", type=int, default=MAIN_PORT,
                        help=f"MainServer 포트 (기본: {MAIN_PORT})")
    args = parser.parse_args()

    print(f"▶ MainServer 연결: {args.host}:{args.port}")
    print(f"▶ 스테이션: {args.station}, NG 전송 수: {args.count}")
    print()

    try:
        sock = socket.create_connection((args.host, args.port), timeout=5.0)
    except Exception as e:
        print(f"❌ 연결 실패: {e}")
        return 1

    print(f"✅ 연결 성공 (local={sock.getsockname()}, remote={sock.getpeername()})")
    print()

    success = 0
    fail = 0

    try:
        # 1. INSPECT_META (OK 건 전송)
        print("── 1) INSPECT_META 전송 ──")
        test_inspect_meta(sock, args.station)
        time.sleep(0.2)

        # 2. OK 카운트 전송
        print("\n── 2) STATION_OK_COUNT 전송 ──")
        test_ok_count(sock, args.station)
        time.sleep(0.2)

        # 3. NG 패킷 반복 전송 + ACK 확인
        print(f"\n── 3) STATION{args.station}_NG x {args.count}회 전송 ──")
        for i in range(1, args.count + 1):
            if test_ng_packet(sock, args.station, i):
                success += 1
            else:
                fail += 1
            time.sleep(0.3)

        # 4. 결과 요약
        print("\n══════════════════════════════════════════")
        print(f"✅ 성공: {success} / {args.count}")
        print(f"❌ 실패: {fail} / {args.count}")
        print()
        print("▶ MainServer 로그에서 'INSERT inspections' 메시지 확인")
        print("▶ DB 확인: mysql -u factorymanager -p1234 Factory")
        print("         -e 'SELECT id, station_id, result, timestamp FROM inspections ORDER BY id DESC LIMIT 5;'")

    except KeyboardInterrupt:
        print("\n중단됨")
    except Exception as e:
        print(f"\n❌ 예외: {e}")
        return 2
    finally:
        sock.close()

    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
