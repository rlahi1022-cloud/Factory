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

## 모델 주입 지점

`Common/Inferencer.py`의 `Station1Inferencer.infer()` /
`Station2Inferencer.infer()` 안에서 PatchCore / YOLO11 호출을
구현하기만 하면 파이프라인은 그대로 동작.

```python
def infer(self, image):
    # 학습된 모델 호출 후
    return {"result": "NG", "score": 0.87, "defect": "contamination"}
```

반환 dict의 최소 필수 키:

| 키 | 타입 | 설명 |
|----|------|------|
| `result` | str | "OK" 또는 "NG" |
| `score` | float | 이상 점수 / 신뢰도 |
| `defect` | str | 결함 유형 (OK면 빈 문자열) |

추가 필드(예: `detections`, `heatmap`)는 자유롭게 포함 가능 — Sender가 그대로 NG 패킷 본문에 실어 보냄.
