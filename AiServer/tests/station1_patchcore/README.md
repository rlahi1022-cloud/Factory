# Station1 PatchCore 디버그/테스트

Station1 입고검사 PatchCore(Anomalib) 추론 관련 디버그 및 테스트 스크립트.

## 파일 목록

| 파일 | 용도 |
|------|------|
| `DebugInference.py` | Anomalib 모델 출력 객체 구조 확인 |
| `DebugMask.py` | `pred_mask` 반환값 실제 shape/dtype 확인 |
| `DebugModel.py` | 모델 가중치/메모리뱅크 로드 검증 |
| `DebugThreshold.py` | 정상/불량 raw 점수 분포 확인 (임계값 튜닝) |
| `TestAnomalibPredict.py` | Anomalib `Engine.predict()` 공식 파이프라인 추론+시각화 |

## 실행 방법

```bash
cd AiServer
python -m tests.station1_patchcore.DebugThreshold --model models/station1_patchcore.ckpt
python -m tests.station1_patchcore.TestAnomalibPredict --model models/station1_patchcore.ckpt \
    --dir data/station1/test
```

## 참고
- 공용 단일/배치 추론 스크립트는 루트 `tests/TestInference.py`, `tests/TestBatchInference.py` 참조
- 학습은 `Training/TrainPatchcore.py` 사용
