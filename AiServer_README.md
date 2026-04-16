# AiServer — Python asyncio.Queue 파이프라인

## 파이프라인

```
[Pylon Grab Producer] --asyncio.Queue--> [Inference Worker]
                                              |
                          ┌───────────────────┼───────────────────┐
                          v                   v                   v
                  INSPECT_META(1006)    OK(카운터 누적)        NG → result_queue
                  (fire-and-forget)           |                   |
                                              v                   v
                                  [OK Count Reporter]      [Sender Worker]
                                  5초 주기 1004 송신        STATION_NG(1000/1002)
                                                            ACK 대기 + 재전송
```

## 설계 원칙

- 단계마다 `asyncio.Queue`로 분리 → 백프레셔 자동.
- 추론은 CPU/GPU 바운드이므로 `loop.run_in_executor`로 별도 스레드에서 호출.
- NG는 ACK 기반 송신, OK는 카운터만 누적 후 주기 송신.
- 모든 검사(OK/NG) 직후 `INSPECT_META(1006)`를 fire-and-forget 송신 → 메인서버 `inspections` 테이블 기록용.
- TCP 클라이언트는 끊김 시 자동 재연결 + 백그라운드 receiver가 ACK 라우팅.
- HEALTH_PING(1200) 수신 시 HEALTH_PONG(1201) 자동 응답.
- MODEL_RELOAD_CMD(1010) 수신 시 콜백 실행 + MODEL_RELOAD_RES(1011) 응답.
- 종료는 sentinel 객체를 큐에 주입 → 워커가 자연 종료.

## inspection_id 발급 규칙

`stationN-YYYYMMDDHHMMSSmmm-seq` 형식으로 추론서버에서 발급.

예: `station1-20260415120000123-000042`

- `N`: 스테이션 번호 (1 또는 2)
- `YYYYMMDDHHMMSSmmm`: UTC 타임스탬프 (밀리초 포함)
- `seq`: 6자리 시퀀스 (추론서버 내부 카운터)

## 코루틴 구성

| 코루틴 | 개수 | 역할 |
|--------|------|------|
| `_run_grab_producer` | 1 | Pylon 카메라 grab → grab_queue |
| `_run_inference_worker` | N (Config) | grab_queue 소비 → 추론 → INSPECT_META 송신 + (NG면 result_queue) |
| `_run_sender_worker` | N (Config) | result_queue 소비 → STATION_NG 송신 + ACK 대기/재전송 |
| `_run_ok_count_reporter` | 1 | 5초 주기로 OK/NG 누적 카운트 STATION_OK_COUNT 송신 |

## 추론 모델

### Station1 — PatchCore (입고 검사)

| 항목 | 내용 |
|------|------|
| 모델 | PatchCore (anomalib 기반) |
| 입력 | 224x224 BGR |
| 출력 | anomaly_score + heatmap |
| 판정 | score > threshold → NG |
| 결함 분류 | score 구간별 (scratch, dent, contamination, deformation) |

### Station2 — YOLO11 + PatchCore (조립 검사)

| 항목 | 내용 |
|------|------|
| 1차 모델 | YOLO11 객체 탐지 (cap, label, liquid_level) |
| 2차 모델 | PatchCore 표면 이상탐지 |
| 판정 | YOLO 결함 감지 OR PatchCore 이상 → NG |
| 출력 필드 | cap_ok, label_ok, fill_ok, detections, patchcore_score |

## AI서버가 보내는 NG 패킷 필드

### Station1 (STATION1_NG = 1000)

```json
{
  "protocol_no": 1000,
  "protocol_version": "1.0",
  "inspection_id": "station1-20260416120000123-000001",
  "station_id": 1,
  "result": "NG",
  "defect": "crack",
  "score": 0.87,
  "latency_ms": 45,
  "timestamp": "2026-04-16T12:00:00.123",
  "image_size": 102400
}
+ [이미지 바이너리]
```

### Station2 (STATION2_NG = 1002)

```json
{
  "protocol_no": 1002,
  "protocol_version": "1.0",
  "inspection_id": "station2-20260416120000456-000001",
  "station_id": 2,
  "result": "NG",
  "defect": "cap_missing",
  "defects": ["cap_missing"],
  "score": 0.92,
  "cap_ok": 0,
  "label_ok": 1,
  "fill_ok": 1,
  "detections": [{"class": "cap_missing", "confidence": 0.92, "bbox": [10, 20, 100, 120]}],
  "patchcore_score": 0.42,
  "latency_ms": 52,
  "timestamp": "2026-04-16T12:00:00.456",
  "image_size": 98000
}
+ [이미지 바이너리]
```

## 학습 서버

| 파일 | 역할 |
|------|------|
| `Training/TrainingMain.py` | TCP 서버 (포트 9100), 학습 요청 수신/진행률 송신 |
| `Training/TrainPatchcore.py` | PatchCore 비지도 학습 (anomalib) |
| `Training/TrainYolo.py` | YOLO11 전이학습 (ultralytics) |
| `Training/TrainingConfig.py` | 학습 설정 dataclass |

## 모델 주입 지점

`Common/Inferencer.py`의 `Station1Inferencer.infer()` /
`Station2Inferencer.infer()` 에서 PatchCore / YOLO11 추론 구현 완료.

```python
# Station1 반환 예시
{"result": "NG", "score": 0.87, "defect": "crack", "heatmap": ndarray}

# Station2 반환 예시
{"result": "NG", "score": 0.92, "defect": "cap_missing",
 "defects": ["cap_missing"], "detections": [...],
 "patchcore_score": 0.42, "cap_ok": 0, "label_ok": 1, "fill_ok": 1}
```

반환 dict의 최소 필수 키:

| 키 | 타입 | 설명 |
|----|------|------|
| `result` | str | "OK" 또는 "NG" |
| `score` | float | 이상 점수 / 신뢰도 |
| `defect` | str | 결함 유형 (OK면 빈 문자열) |

추가 필드(예: `detections`, `heatmap`, `cap_ok`)는 자유롭게 포함 가능 — Sender가 그대로 NG 패킷 본문에 실어 보냄.
