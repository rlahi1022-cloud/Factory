"""TestPipeline.py — AI 추론서버 역할 시뮬레이션 (numpy/torch 없이 순수 TCP)

실제 Station1/Station2 추론서버가 있다면 카메라 → 추론 → 전송 파이프라인을
거치지만, 이 스크립트는 **가짜 NG 결과**를 직접 만들어 MainServer로 전송한다.

왜 필요한가:
  - 실제 카메라/모델 없이도 MainServer의 TCP/DB 동작을 테스트 가능.
  - PyTorch/OpenCV 같은 무거운 라이브러리 없이 가볍게 실행 가능.
  - 프로토콜 통신만 빠르게 검증하고 싶을 때 유용.

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

# __future__.annotations: 타입 힌트를 문자열로 지연 평가 (Python 3.10 미만 호환)
from __future__ import annotations

# argparse: 커맨드라인 인자(--station, --count 등)를 파싱하는 표준 라이브러리
import argparse

# json: 패킷 본문을 JSON으로 인코딩/디코딩
import json

# socket: 저수준 TCP 소켓 통신 (asyncio 대신 동기 소켓 사용 — 단순한 테스트용)
import socket

# struct: 바이너리 헤더(4바이트 big-endian 길이)를 다루기 위한 라이브러리
import struct

# sys: 프로그램 종료 코드 설정용 (sys.exit)
import sys

# time: 패킷 사이 간격 대기용
import time

# datetime: 타임스탬프 생성 (ISO 8601 형식)
from datetime import datetime, timezone

# Path: 파일 경로 처리
from pathlib import Path


# ═════════════════════════════════════════════
# 설정 (config.json에서 로드)
# ═════════════════════════════════════════════
# 이 스크립트 파일(TestPipeline.py)의 절대 경로
_THIS = Path(__file__).resolve()
# Factory/config/config.json 경로 계산
# TestPipeline.py는 Factory/AiServer/tests/에 있으므로 parent.parent.parent = Factory
_CONFIG = _THIS.parent.parent.parent / "config" / "config.json"

# config.json 파일을 UTF-8로 읽어서 JSON 파싱
with open(_CONFIG, "r", encoding="utf-8") as f:
    _cfg = json.load(f)

# 네트워크 설정에서 운용서버 IP와 포트 추출
MAIN_HOST = _cfg["network"]["main_server_host"]
MAIN_PORT = _cfg["network"]["main_server_ai_port"]


# ═════════════════════════════════════════════
# 프로토콜 번호 (Protocol.py와 동일해야 함)
# ═════════════════════════════════════════════
# AiServer/Common/Protocol.py의 ProtocolNo 열거형과 값이 일치해야 한다.
# 여기서 따로 정의하는 이유: 이 테스트 스크립트를 다른 서버의 모듈 의존성 없이
# 독립적으로 실행할 수 있게 하기 위함.
STATION1_NG         = 1000   # 추론#1 → 운용: 입고검사 NG 결과
STATION2_NG         = 1002   # 추론#2 → 운용: 조립검사 NG 결과
STATION_OK_COUNT    = 1004   # 추론 → 운용: OK 카운트 주기 보고
INSPECT_META        = 1006   # 추론 → 운용: 검사 메타데이터 (OK/NG 공통)
STATION1_NG_ACK     = 1001   # 운용 → 추론#1: NG 수신 확인
STATION2_NG_ACK     = 1003   # 운용 → 추론#2: NG 수신 확인


# ═════════════════════════════════════════════
# 가짜 JPEG 이미지 (테스트용 더미 데이터)
# ═════════════════════════════════════════════
# 실제 JPEG 파일의 최소 유효 헤더 + 1KB의 더미 바이트.
# MainServer가 이미지를 받아 저장/검증만 하면 충분하므로,
# 실제 이미지 내용은 필요 없다.
FAKE_JPEG = bytes([
    # JPEG SOI 마커 (Start of Image): 0xFFD8
    0xFF, 0xD8,
    # APP0 마커 + JFIF 헤더 (JPEG 파일임을 표시)
    0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
]) + bytes(1024)  # 뒤에 1KB(1024바이트)의 0으로 채워진 더미 추가


# ═════════════════════════════════════════════
# 유틸리티 함수
# ═════════════════════════════════════════════

def _now_iso() -> str:
    """현재 시각을 ISO 8601 문자열로 반환한다.

    반환값:
      str: "2026-04-18T15:30:45.123+00:00" 형식의 UTC 시각
    """
    # timezone.utc: UTC 시간대, isoformat: ISO 8601 포맷, milliseconds: 밀리초까지만
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _build_packet(body_dict: dict, image_bytes: bytes = b"") -> bytes:
    """패킷 빌드: [4byte BE length] + [JSON UTF-8] + [image]

    용도:
      MainServer로 전송할 패킷을 만든다.
      프로토콜 형식: 4바이트 JSON 길이(big-endian) + JSON 본문 + 이미지 바이너리

    매개변수:
      body_dict (dict): JSON으로 인코딩할 본문
      image_bytes (bytes): 이미지 바이너리 (없으면 빈 바이트)

    반환값:
      bytes: 전송 가능한 완성된 패킷
    """
    # 프로토콜 버전과 이미지 크기를 body에 자동 주입
    body_dict["protocol_version"] = "1.0"
    body_dict["image_size"] = len(image_bytes)

    # dict → JSON 문자열 변환 (한국어 그대로 유지)
    json_str = json.dumps(body_dict, ensure_ascii=False)
    # UTF-8 바이트로 인코딩
    json_bytes = json_str.encode("utf-8")

    # 4바이트 big-endian 헤더 생성 (JSON 크기를 담음)
    # ">I": big-endian unsigned 32-bit integer
    header = struct.pack(">I", len(json_bytes))

    # 헤더 + JSON + 이미지 순서로 합쳐서 반환
    return header + json_bytes + image_bytes


def _recv_exactly(sock: socket.socket, n: int, timeout: float = 2.0) -> bytes:
    """정확히 n바이트를 수신한다 (타임아웃 지원).

    용도:
      TCP는 한 번의 recv로 요청한 만큼 안 올 수 있으므로,
      필요한 n바이트를 모두 받을 때까지 반복 수신한다.

    매개변수:
      sock (socket): 수신할 소켓
      n (int): 받을 바이트 수
      timeout (float): 각 recv 호출의 최대 대기 시간 (초)

    반환값:
      bytes: n바이트의 수신 데이터

    예외:
      ConnectionError: 상대가 연결을 끊었을 때
    """
    # 소켓 타임아웃 설정 (응답 없으면 예외 발생)
    sock.settimeout(timeout)
    data = b""

    # n바이트를 모두 받을 때까지 반복
    while len(data) < n:
        # 아직 받을 바이트 수만큼만 요청
        chunk = sock.recv(n - len(data))
        # recv가 0바이트 반환 = 상대가 연결 종료
        if not chunk:
            raise ConnectionError("연결 끊김")
        data += chunk
    return data


def _recv_one_packet(sock: socket.socket, timeout: float = 2.0) -> dict:
    """서버로부터 패킷 1개 수신 (주로 ACK 응답용).

    패킷 형식: [4byte 길이] + [JSON] + [이미지 (선택)]

    매개변수:
      sock (socket): 수신 소켓
      timeout (float): 타임아웃

    반환값:
      dict: 파싱된 JSON 본문
    """
    # 1단계: 4바이트 헤더 수신
    header = _recv_exactly(sock, 4, timeout)
    # big-endian unsigned int로 해석하여 JSON 크기 추출
    json_size = struct.unpack(">I", header)[0]

    # 비정상 크기 체크 (0 또는 64KB 초과)
    if json_size == 0 or json_size > 64 * 1024:
        raise ValueError(f"비정상 JSON 크기: {json_size}")

    # 2단계: JSON 본문 수신
    body = _recv_exactly(sock, json_size, timeout)
    # UTF-8 디코딩 후 JSON 파싱
    msg = json.loads(body.decode("utf-8"))

    # 3단계: 이미지 바이너리가 있다면 추가로 수신
    # ACK에는 보통 이미지가 없지만, 안전 처리를 위해 확인
    img_size = int(msg.get("image_size", 0))
    if img_size > 0:
        _recv_exactly(sock, img_size, timeout)  # 받아서 버림

    return msg


# ═════════════════════════════════════════════
# 테스트 시나리오 함수들
# ═════════════════════════════════════════════

def test_ng_packet(sock: socket.socket, station_id: int, seq: int) -> bool:
    """NG 패킷 1건 전송 후 ACK 확인 (핵심 테스트).

    용도:
      실제 추론서버가 불량을 감지했을 때 보내는 NG 패킷을 시뮬레이션.
      MainServer가 DB에 저장하고 ACK를 돌려주는지 검증.

    매개변수:
      sock (socket): MainServer와의 연결 소켓
      station_id (int): 1(입고) 또는 2(조립)
      seq (int): 일련번호 (inspection_id 생성용)

    반환값:
      bool: ACK 수신 성공 시 True, 실패 시 False
    """
    # 현재 시각 타임스탬프
    ts = _now_iso()

    # inspection_id 형식: "stationN-YYYYMMDDHHMMSSmmm-NNNNNN"
    # UTC 시간을 연월일시분초밀리초 문자열로 변환
    dt_str = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")[:-3]  # [:-3]은 마이크로초 → 밀리초로 자르기
    inspection_id = f"station{station_id}-{dt_str}-{seq:06d}"  # seq를 6자리 0채움

    # 스테이션별 프로토콜 번호 선택
    proto_no = STATION1_NG if station_id == 1 else STATION2_NG
    ack_no   = STATION1_NG_ACK if station_id == 1 else STATION2_NG_ACK

    # NG 결과 본문 구성
    body = {
        "protocol_no": proto_no,
        "inspection_id": inspection_id,
        "station_id": station_id,
        "result": "ng",
        # Inferencer.py와 동일 필드명 사용 ("defect", defect_type 아님)
        "defect": "anomaly" if station_id == 1 else "cap_loose",
        "score": 0.87,           # 예시 이상 점수
        "latency_ms": 45,        # 추론 소요 시간 (밀리초)
        "timestamp": ts,
    }

    # Station2는 추가 필드 포함 (조립 상세 검사 결과)
    if station_id == 2:
        body.update({
            "cap_ok": 0,             # 캡 불량 (0=불량, 1=정상)
            "label_ok": 1,           # 라벨 정상
            "fill_ok": 1,            # 충전량 정상
            "patchcore_score": 0.72, # 라벨 표면 이상 점수
            # YOLO 탐지 결과
            "detections": [
                {"class": "cap_loose", "confidence": 0.87, "bbox": [100, 150, 200, 250]}
            ],
        })

    # 이미지를 포함한 패킷 생성
    packet = _build_packet(body, FAKE_JPEG)

    # 전송 로그 출력
    print(f"  [TX] NG station={station_id} id={inspection_id} ({len(packet)} bytes)")
    # sendall: 전체 데이터가 전송될 때까지 블로킹
    sock.sendall(packet)

    # ACK 수신 대기 (3초 타임아웃)
    try:
        ack = _recv_one_packet(sock, timeout=3.0)
        # 프로토콜 번호와 ack=true 플래그 확인
        if ack.get("protocol_no") == ack_no and ack.get("ack") is True:
            print(f"  [RX] ACK OK id={ack.get('inspection_id')}")
            return True
        else:
            # 기대했던 ACK가 아니면 NACK로 간주
            print(f"  [RX] NACK? {ack}")
            return False
    except Exception as e:
        # 타임아웃 또는 연결 에러
        print(f"  [ERR] ACK 수신 실패: {e}")
        return False


def test_ok_count(sock: socket.socket, station_id: int) -> None:
    """OK 카운트 fire-and-forget 전송 (ACK 없음).

    용도:
      주기적으로 "OK/NG 건수"를 운용서버에 보고.
      ACK가 불필요한 단순 통계 전송.

    매개변수:
      sock (socket): 전송 소켓
      station_id (int): 스테이션 번호
    """
    # 통계 본문 구성 (예시 값)
    body = {
        "protocol_no": STATION_OK_COUNT,
        "station_id": station_id,
        "ok_count": 152,         # 누적 OK 건수
        "ng_count": 3,           # 누적 NG 건수
        "latency_avg": 42.5,     # 평균 추론 시간 (ms)
        "period": "5s",          # 집계 주기
        "timestamp": _now_iso(),
    }
    # 이미지 없는 패킷 (통계만)
    packet = _build_packet(body)
    print(f"  [TX] OK_COUNT station={station_id} ok=152 ng=3 (fire-and-forget)")
    sock.sendall(packet)
    # ACK 수신 대기 안 함 (fire-and-forget)


def test_inspect_meta(sock: socket.socket, station_id: int) -> None:
    """검사 메타데이터 (OK 건) 전송.

    용도:
      OK 판정 건도 DB에는 메타데이터만 기록하기 위함 (이미지는 저장 안 함).

    매개변수:
      sock (socket): 전송 소켓
      station_id (int): 스테이션 번호
    """
    # inspection_id 생성 (OK용)
    dt_str = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")[:-3]
    body = {
        "protocol_no": INSPECT_META,
        "inspection_id": f"station{station_id}-{dt_str}-meta",
        "station_id": station_id,
        "result": "ok",          # 정상 결과
        "score": 0.05,           # 낮은 이상 점수 (정상)
        "latency_ms": 38,
        "timestamp": _now_iso(),
    }
    packet = _build_packet(body)
    print(f"  [TX] INSPECT_META station={station_id} result=ok (fire-and-forget)")
    sock.sendall(packet)


# ═════════════════════════════════════════════
# 메인 진입점
# ═════════════════════════════════════════════

def main() -> int:
    """메인 함수: 커맨드라인 인자 파싱 후 테스트 시나리오 실행.

    반환값:
      int: 종료 코드 (0=모두 성공, 1=일부 실패, 2=예외 발생)
    """
    # 커맨드라인 인자 파서 생성
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

    # 테스트 시작 헤더 출력
    print(f"▶ MainServer 연결: {args.host}:{args.port}")
    print(f"▶ 스테이션: {args.station}, NG 전송 수: {args.count}")
    print()

    # TCP 연결 시도 (5초 타임아웃)
    try:
        sock = socket.create_connection((args.host, args.port), timeout=5.0)
    except Exception as e:
        # 연결 실패 (서버가 꺼져있거나 IP/포트 오류)
        print(f"❌ 연결 실패: {e}")
        return 1

    # 연결 정보 출력 (로컬 IP:포트, 원격 IP:포트)
    print(f"✅ 연결 성공 (local={sock.getsockname()}, remote={sock.getpeername()})")
    print()

    # 성공/실패 카운터
    success = 0
    fail = 0

    try:
        # ── 1) INSPECT_META 전송 (OK 건 시뮬레이션) ──
        print("── 1) INSPECT_META 전송 ──")
        test_inspect_meta(sock, args.station)
        time.sleep(0.2)  # 서버 처리 시간 고려 대기

        # ── 2) OK 카운트 전송 ──
        print("\n── 2) STATION_OK_COUNT 전송 ──")
        test_ok_count(sock, args.station)
        time.sleep(0.2)

        # ── 3) NG 패킷 반복 전송 + ACK 확인 ──
        # 실제 불량 탐지 시나리오 시뮬레이션
        print(f"\n── 3) STATION{args.station}_NG x {args.count}회 전송 ──")
        for i in range(1, args.count + 1):
            if test_ng_packet(sock, args.station, i):
                success += 1
            else:
                fail += 1
            time.sleep(0.3)  # 패킷 사이 간격

        # ── 4) 결과 요약 ──
        print("\n══════════════════════════════════════════")
        print(f"✅ 성공: {success} / {args.count}")
        print(f"❌ 실패: {fail} / {args.count}")
        print()
        print("▶ MainServer 로그에서 'INSERT inspections' 메시지 확인")
        print("▶ DB 확인: mysql -u factorymanager -p1234 Factory")
        print("         -e 'SELECT id, station_id, result, timestamp FROM inspections ORDER BY id DESC LIMIT 5;'")

    except KeyboardInterrupt:
        # Ctrl+C로 중단 시
        print("\n중단됨")
    except Exception as e:
        # 예상치 못한 예외
        print(f"\n❌ 예외: {e}")
        return 2
    finally:
        # 어떤 경로로든 소켓은 반드시 닫는다
        sock.close()

    # 모두 성공했으면 0, 하나라도 실패하면 1
    return 0 if fail == 0 else 1


# 스크립트 직접 실행 시에만 main() 호출
if __name__ == "__main__":
    sys.exit(main())
