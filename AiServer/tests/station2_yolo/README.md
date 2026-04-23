# Station2 YOLO 테스트

Station2 조립검사에서 **YOLO11 객체 탐지**(cap / label / liquid_level) 관련
테스트 및 디버그 스크립트 보관용 폴더.

## 범위
- YOLO 모델 로드/추론 테스트
- 바운딩 박스 시각화 확인
- 클래스별 Precision/Recall 분석
- IoU 기반 위치 정상성 판정 검증

## 참고
- 공용 스크립트 `tests/TestInference.py`, `tests/TestBatchInference.py` 는
  `--station 2` 인자로 Station2 하이브리드 추론(YOLO+PatchCore)을 실행함.
- 해당 공용 스크립트는 루트 tests/에 유지.
