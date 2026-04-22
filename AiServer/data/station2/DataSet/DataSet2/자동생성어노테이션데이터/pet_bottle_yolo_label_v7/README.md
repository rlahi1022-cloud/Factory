# PET Bottle Auto-Labeling Output

## 구성
- `images/train`, `images/val` — 원본 BMP 이미지 (80/20 split, 카테고리별 stratified)
- `labels/train`, `labels/val` — YOLO11 포맷 라벨 (`class_id cx cy w h`, 0–1 정규화)
- `previews/` — 박스가 그려진 시각화 이미지 (검수용)
- `review.csv` — 이미지별 검출 요약 및 플래그
- `data.yaml` — YOLO 학습에 바로 쓰는 config

## 클래스
- 0 = cap (캡)
- 1 = label (라벨)
- 2 = fill_level (충진량) — 캡 바로 아래(또는 meniscus)부터 병 바닥까지

## 검수 우선순위
`review.csv`의 `flags` 컬럼이 비어있지 않은 이미지부터 확인.
- `MISSING_CAP/LABEL` — 있어야 할 게 빠진 경우
- `UNEXPECTED_CAP/LABEL` — 없어야 할 게 잡힌 경우
- `MISSING_FILL` — 충진량 박스가 안 잡힌 경우 (드묾)

## 검수 도구
[LabelImg](https://github.com/HumanSignal/labelImg) 또는
[X-AnyLabeling](https://github.com/CVHub520/X-AnyLabeling)에
`images/train` 또는 `images/val` 폴더를 열면 자동으로 `.txt` 라벨이 로드됩니다.

## YOLO11 학습 예시
```bash
pip install ultralytics
yolo detect train data=data.yaml model=yolo11n.pt epochs=100 imgsz=1280
```

## 알려진 한계
- `충전량` 케이스에서 fill_level 박스의 상단은 실제 액체 표면이 아니라
  캡 바로 아래에서 시작합니다. 일관된 위치로 통일해서 모델이 박스 내부
  픽셀 패턴으로 fill 상태를 학습할 수 있도록 한 의도적 설계입니다.
  엄밀한 meniscus 감지가 필요하면 해당 이미지들은 수동 보정 권장.
- 라벨 박스는 라벨이 완전히 감싸지 않는 경우(라벨각도다름 등)
  미세한 조정이 필요할 수 있습니다.
