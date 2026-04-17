# 통신 프로토콜

## 패킷 구조

```
[4 byte length, big-endian uint32]   # JSON 본문 크기
[JSON payload]                       # 공통 필드 + 메시지별 본문
[Image binary, image_size bytes]     # NG 이미지 동봉 시
```

## JSON 공통 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `protocol_no` | int | 메시지 번호 (필수) |
| `protocol_version` | str | "1.0" |
| `inspection_id` | str | 검사 결과 계열 필수, `stationN-YYYYMMDDHHMMSSmmm-seq` 형식 |
| `request_id` | str | 요청/응답 매칭 (optional) |
| `station_id` | int | 1 또는 2 |
| `timestamp` | str | ISO8601 |
| `image_size` | int | 이미지 동봉 시 양수, 아니면 0 |

참고: C++ 코드에서는 `FACTORY_PROTOCOL_VERSION` 상수명 사용 (MariaDB 헤더 매크로 충돌 방지)

## 메시지 번호 (요구사항 분석서 기준)

### 외부 채널 (MFC ↔ 운용) — 100~199

| 번호 | 이름 | 방향 | 구현 상태 |
|------|------|------|----------|
| 100 | LOGIN_REQ | MFC → 운용 | ✅ 완성 (DB 인증) |
| 101 | LOGIN_RES | 운용 → MFC | ✅ 완성 |
| 102 | LOGOUT_REQ | MFC → 운용 | ✅ 완성 |
| 103 | LOGOUT_RES | 운용 → MFC | ✅ 완성 |
| 104 | REGISTER_REQ | MFC → 운용 | ✅ 완성 (DB INSERT) |
| 105 | REGISTER_RES | 운용 → MFC | ✅ 완성 |
| 110 | INSPECT_NG_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier) |
| 112 | INSPECT_OK_COUNT_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier) |
| 114 | INSPECT_HISTORY_REQ | MFC → 운용 | ✅ 완성 (DB 조회) |
| 115 | INSPECT_HISTORY_RES | 운용 → MFC | ✅ 완성 |
| 130 | STATS_REQ | MFC → 운용 | ✅ 완성 (DB 집계) |
| 131 | STATS_RES | 운용 → MFC | ✅ 완성 |
| 150 | MODEL_LIST_REQ | MFC → 운용 | ✅ 완성 (DB 조회) |
| 151 | MODEL_LIST_RES | 운용 → MFC | ✅ 완성 |
| 152 | RETRAIN_REQ | MFC → 운용 | ✅ 완성 (학습서버 TCP 중계) |
| 153 | RETRAIN_RES | 운용 → MFC | ✅ 완성 |
| 154 | RETRAIN_PROGRESS_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier) |
| 170 | SERVER_HEALTH_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier) |

### 내부 채널 (운용 ↔ 추론) — 1000~1099, 완성

| 번호 | 이름 | 방향 | ACK |
|------|------|------|-----|
| 1000 | STATION1_NG | 추론#1 → 운용 | 필수 (1001) |
| 1001 | STATION1_NG_ACK | 운용 → 추론#1 | — |
| 1002 | STATION2_NG | 추론#2 → 운용 | 필수 (1003) |
| 1003 | STATION2_NG_ACK | 운용 → 추론#2 | — |
| 1004 | STATION_OK_COUNT | 추론 → 운용 | Fire-and-forget |
| 1006 | INSPECT_META | 추론 → 운용 | Fire-and-forget |
| 1010 | MODEL_RELOAD_CMD | 운용 → 추론 | 필수 (1011) |
| 1011 | MODEL_RELOAD_RES | 추론 → 운용 | — |

### 학습 채널 (운용 ↔ 학습) — 1100~1199

| 번호 | 이름 | 방향 | 구현 상태 |
|------|------|------|----------|
| 1100 | TRAIN_START_REQ | 운용 → 학습 | ✅ 양쪽 완성 |
| 1101 | TRAIN_START_RES | 학습 → 운용 | ✅ AI서버 완성 |
| 1102 | TRAIN_PROGRESS | 학습 → 운용 | ✅ 양쪽 완성 (Router + GuiNotifier 푸시) |
| 1104 | TRAIN_COMPLETE | 학습 → 운용 | ✅ 양쪽 완성 (Router + DbManager INSERT + ACK) |
| 1105 | TRAIN_COMPLETE_ACK | 운용 → 학습 | ✅ 메인서버 완성 |
| 1106 | TRAIN_FAIL | 학습 → 운용 | ✅ 양쪽 완성 (Router + GuiNotifier 푸시) |
| 1107 | TRAIN_FAIL_ACK | 운용 → 학습 | ✅ 메인서버 완성 |

### 헬스체크 — 1200~

| 번호 | 이름 | 방향 | 구현 상태 |
|------|------|------|----------|
| 1200 | HEALTH_PING | 운용 → 각 서버 | 메인서버: TCP connect만 |
| 1201 | HEALTH_PONG | 각 서버 → 운용 | AI서버: 자동응답 완성 |

### 내부 공통 — 1900~

| 번호 | 이름 | 방향 |
|------|------|------|
| 1900 | INTERNAL_ACK | 양방향 |
| 1901 | INTERNAL_NACK | 양방향 |
| 1902 | INTERNAL_RETRY_REQ | 양방향 |
| 1903 | INTERNAL_RETRY_DATA | 양방향 |
| 1904 | INTERNAL_ERROR | 양방향 |

## ACK / 재전송 정책

- **ACK 필수 메시지**: STATION1/2_NG, MODEL_RELOAD_CMD, TRAIN_COMPLETE/FAIL, INSPECT_NG_PUSH, MODEL_DEPLOY_NOTIFY
- **타임아웃**: 1초
- **최대 재전송**: 3회
- **NACK 수신 시**: 재전송하지 않고 drop + 에러 로그
- **메인서버 동작**: NG 패킷 수신 → DB INSERT 성공 시 같은 connection으로 ACK 회신, 실패 시 NACK

## 포트 할당

| 포트 | 용도 | 프로세스 |
|------|------|---------|
| 9000 | 추론서버 → 메인서버 | MainServer (TcpListener) |
| 9010 | MFC → 메인서버 | MainServer (GuiTcpListener) |
| 9100 | 메인서버 → 학습서버 | AiServer (TrainingMain) |
| 9101 | 헬스체크 대상 (추론#1) | AiServer (Station1) |
| 9102 | 헬스체크 대상 (추론#2) | AiServer (Station2) |
| 9201 | 헬스체크 대상 (학습) | AiServer (Training) |

## 송신 예시 (Python)

```python
from Common.Protocol import ProtocolNo
from Common.Packet import PacketBuilder

# NG 송신 (ACK 필수)
packet = PacketBuilder.build_packet(
    protocol_no=int(ProtocolNo.STATION1_NG),
    body_dict={
        "station_id": 1,
        "result": "NG",
        "defect": "contamination",
        "score": 0.87,
        "latency_ms": 45,
        "timestamp": "2026-04-15T12:00:00.123+00:00",
    },
    inspection_id="station1-20260415120000123-000042",
    image_bytes=jpg_bytes,
)
ok = await tcp_client.send_with_ack(packet, int(ProtocolNo.STATION1_NG),
                                    "station1-20260415120000123-000042")

# OK 카운트 (fire-and-forget)
packet = PacketBuilder.build_packet(
    protocol_no=int(ProtocolNo.STATION_OK_COUNT),
    body_dict={
        "station_id": 1,
        "ok_count": 152,
        "ng_count": 3,
        "latency_avg": 42.5,
        "period": "5s",
    },
)
await tcp_client.send_fire_and_forget(packet)
```
