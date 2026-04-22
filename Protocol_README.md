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
| 110 | INSPECT_NG_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier, 이미지 바이너리 첨부) |
| 112 | INSPECT_OK_COUNT_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier) |
| 114 | INSPECT_HISTORY_REQ | MFC → 운용 | ✅ 완성 (DB 조회) |
| 115 | INSPECT_HISTORY_RES | 운용 → MFC | ✅ 완성 |
| 130 | STATS_REQ | MFC → 운용 | ✅ 완성 (DB 집계) |
| 131 | STATS_RES | 운용 → MFC | ✅ 완성 |
| 150 | MODEL_LIST_REQ | MFC → 운용 | ✅ 완성 (DB 조회) |
| 151 | MODEL_LIST_RES | 운용 → MFC | ✅ 완성 |
| 152 | RETRAIN_REQ | MFC → 운용 | ✅ 완성 (v0.13.0: session_id 필드 추가) |
| 153 | RETRAIN_RES | 운용 → MFC | ✅ 완성 |
| 154 | RETRAIN_PROGRESS_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier) |
| 158 | RETRAIN_UPLOAD | MFC → 운용 | ✅ v0.13.0 (JSON+binary) |
| 159 | RETRAIN_UPLOAD_ACK | 운용 → MFC | ✅ v0.13.0 (업로드 진행률) |
| 170 | SERVER_HEALTH_PUSH | 운용 → MFC | ✅ 완성 (GuiNotifier) |

#### RETRAIN_UPLOAD (158) / RETRAIN_UPLOAD_ACK (159) — v0.13.0

클라이언트 "폴더 선택" 후 "재학습 실행" 시, 파일 1장마다 이 프로토콜로 바이너리를 메인에 올리고,
메인은 다시 학습서버(1108) 로 중계. 모든 ACK 수신 후 RETRAIN_REQ(152) + session_id 로 학습 트리거.

**RETRAIN_UPLOAD(158) 본문**:
```json
{
  "protocol_no": 158,
  "request_id": "req-00000123",
  "session_id": "sess-20260422-143057-08712",
  "station_id": 2,
  "model_type": "PatchCore",
  "filename": "sample_042.jpg",
  "file_index": 41,
  "total_files": 100,
  "image_size": 123456,
  "timestamp": "..."
}
[파일 바이너리 123456 바이트]
```

**RETRAIN_UPLOAD_ACK(159) 본문**:
```json
{
  "protocol_no": 159,
  "request_id": "same",
  "session_id": "same",
  "file_index": 41,
  "success": true,
  "saved_path": "./data/station2/uploads/sess-.../sample_042.jpg",
  "message": "",
  "timestamp": "..."
}
```

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
| 1108 | TRAIN_DATA_UPLOAD | 운용 → 학습 | ✅ v0.13.0 (JSON+binary, 학습 데이터 중계) |
| 1109 | TRAIN_DATA_UPLOAD_ACK | 학습 → 운용 | ✅ v0.13.0 |

### 헬스체크 — 1200~

| 번호 | 이름 | 방향 | 구현 상태 |
|------|------|------|----------|
| 1200 | HEALTH_PING | 운용 → 각 서버 | 메인서버: TCP connect만 |
| 1201 | HEALTH_PONG | 각 서버 → 운용 | AI서버: 자동응답 완성 (server_type 포함) |

**v0.11.0 동적 서버 감지**:
HealthChecker 는 더 이상 `config.json` 의 IP 하드코딩에 의존하지 않는다.
Router 가 수신 패킷(inspection/train/HEALTH_PONG 의 `station_id` 또는 `server_type`)
에서 자동으로 server_type 을 추론해 `ConnectionRegistry` 에 태깅 →
HealthChecker 는 `target.name == server_type` 으로 매칭한다.
덕분에 추론/학습 서버를 임의의 PC 에 배치해도 config 수정 없이 자동 감지되고,
로그에는 실제 접속 IP 가 표시된다.

### 내부 공통 — 1900~

| 번호 | 이름 | 방향 |
|------|------|------|
| 1900 | INTERNAL_ACK | 양방향 |
| 1901 | INTERNAL_NACK | 양방향 |
| 1902 | INTERNAL_RETRY_REQ | 양방향 |
| 1903 | INTERNAL_RETRY_DATA | 양방향 |
| 1904 | INTERNAL_ERROR | 양방향 |

## ACK / 재전송 정책

### 일반 메시지 (AI ↔ MainServer)
- **ACK 필수 메시지**: STATION1/2_NG, TRAIN_COMPLETE/FAIL, INSPECT_NG_PUSH, MODEL_DEPLOY_NOTIFY
- **타임아웃**: 3초 (v0.12.0 기준. 기존 1초 → 비동기 분리 후 여유값 3초)
- **최대 재전송**: 3회
- **NACK 수신 시**: 재전송하지 않고 drop + 에러 로그
- **메인서버 동작 (v0.12.0 비동기 분리)**:
  NG 패킷 수신 → `validate_only` (~1ms) → **즉시 ACK 회신** →
  `INSPECTION_VALIDATED` 이벤트로 백그라운드 워커에 위임 →
  워커가 이미지 3장 저장 + DB INSERT + GUI 푸시 수행.
  검증 실패 시 NACK(error_message 포함).
- **Sliced failure 주의**: ACK 가 이미 송신된 뒤 백그라운드 persist 가 실패하면
  AI 서버는 성공으로 간주 → 재전송 없음. 서버 로그에 `[SLICED-FAILURE]` ERROR
  로 기록되며 운영자 확인 필요.

### MODEL_RELOAD_CMD (1010) — 지수 백오프 적용 (v0.8.0)

일시적 네트워크 순단에 대한 복원력 강화.

| 시도 | 재시도 전 대기 |
|-----|--------------|
| 1 | (즉시) |
| 2 | 1초 |
| 3 | 5초 |
| 4 | 30초 |
| 5 | 120초 |
| 실패 | 300초 후 최종 포기 |

**최대 8분 동안 재시도** — 짧은 순단(1~5초) 및 중기 장애(수십 초) 모두 복구 가능.

#### MODEL_RELOAD_CMD JSON 본문 (v0.11.0 갱신)

```json
{
  "protocol_no": 1010,
  "protocol_version": "1.0",
  "station_id": 2,
  "model_type": "PatchCore",   // ← v0.11.0 추가: "PatchCore" | "YOLO11"
  "model_path": "./storage/models/station2/v20260422_1530.ckpt",
  "version":    "v20260422_1530",
  "image_size": 123456789       // 뒤따르는 모델 바이너리 바이트 수
}
```

**브로드캐스트 + 수신측 필터 정책 (v0.11.0)**
- MainServer 는 ConnectionRegistry 에 등록된 **모든 추론서버 연결**로 브로드캐스트한다.
- 추론서버(`StationRunner._handle_model_reload`)가 다음을 검사한다:
  1. `station_id` 가 자신의 `config.station_id` 와 다르면 **조용히 무시**.
  2. Station2 이중모델 구조에서 `model_type` 으로 슬롯 구분:
     - `"YOLO11"`  → `config.model_path`          (YOLO 슬롯)
     - `"PatchCore"` → `config.patchcore_model_path` (PatchCore 슬롯)
  3. `Inferencer.load_model()` 이 양쪽 슬롯을 모두 재로드한다 (바뀌지 않은 슬롯은 동일 파일 재로딩).

## 필수 필드 (중요)

### TRAIN_COMPLETE (1104) 본문 필수 필드
MainServer `TrainService.validate()`가 검증하므로 누락 시 DB INSERT 실패:
- `station_id` (int, 1 또는 2)
- `model_type` (str, 예: "PatchCore", "YOLO11")
- `version` (str)
- `accuracy` (float, 0.0~1.0)
- `model_path` (str)

### TRAIN_FAIL (1106) 본문 필수 필드
GuiNotifier가 클라이언트 푸시 시 사용:
- `station_id`, `model_type`, `error_code`, `message`, `version`

### Timestamp 형식
- 송신 측: ISO8601 (예: `"2026-04-20T12:34:56.789+00:00"`)
- 수신 측: MainServer가 MySQL DATETIME으로 자동 변환
  - `"2026-04-20 12:34:56"` 형식으로 저장

## 포트 할당

| 포트 | 용도 | 프로세스 |
|------|------|---------|
| 9000 | 추론서버 → 메인서버 | MainServer (TcpListener) |
| 9010 | MFC → 메인서버 | MainServer (GuiTcpListener) |
| 9100 | 메인서버 → 학습서버 | AiServer (TrainingMain) |
| 9101 | 헬스체크 대상 (추론#1) | AiServer (Station1) |
| 9102 | 헬스체크 대상 (추론#2) | AiServer (Station2) |
| 9201 | 헬스체크 대상 (학습) | AiServer (Training) |

## 연결 안정성 (v0.7.0~v0.8.0)

### TCP Keepalive (MainServer 수신 소켓)
```
60초 유휴 → probe 시작
10초 간격으로 3회 probe
3회 무응답 시 dead 판정 (최악 90초 내 좀비 감지)
```

### 로그인 시 서버 상태 초기 동기화
- 클라이언트 로그인 직후, 현재 접속된 AI 서버 상태를 `HEALTH_PUSH(170)`로 즉시 전송
- UI LED 3개(학습/추론#1/추론#2)가 초기값이 아닌 실제 상태로 갱신

### UI 연결 상태 실시간 동기화 (클라이언트)
- IDT_STATUSBAR 1초 타이머가 `m_net.IsConnected()` 실측
- silent drop 시 WM_NET_DISCONNECTED 자동 발송
- 10초 후 자동 재접속 시도

### JSON Escape 정책
모든 서버 응답 문자열 필드는 `factory::security::escape_json` 적용 필수.
- `"`, `\`, `\n`, `\r`, `\t`, `\b`, `\f`, 제어문자 → `\uXXXX`

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
