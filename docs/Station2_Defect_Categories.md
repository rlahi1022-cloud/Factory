# Station2 (조립 검사) 결함 카테고리 정리

**작성일**: 2026-04-23
**대상**: `AiServer/Common/Inferencer.py` — `Station2Inferencer`
**범위**: YOLO11 기반 판정 규칙만 (PatchCore 라벨 표면 검사 제외)

---

## 1. 개요

Station2 조립 검사는 YOLO11 로 **3개 클래스**(`cap`, `label`, `liquid_level`)의 위치/존재 여부를 탐지한 뒤, **후처리 로직**으로 OK/NG 판정과 세부 결함 유형을 도출한다.

- **모델이 직접 학습하는 것**: 3개 객체의 바운딩 박스 (위치 + 존재)
- **후처리가 판정하는 것**: 누락, 위치 이탈 → 6가지 결함 코드로 분류

---

## 2. 발생 가능한 결함 전체 목록 (6가지)

### Category A: 누락 (Missing)
YOLO 가 해당 클래스 박스를 **0개** 탐지 → 해당 부품 자체가 없다고 판정

| 결함 코드 | 트리거 조건 | 의미 |
|-----------|-----------|------|
| `cap_missing` | YOLO 가 `cap` 박스 0개 | 캡이 부착되지 않음 |
| `label_missing` | YOLO 가 `label` 박스 0개 | 라벨이 부착되지 않음 |
| `liquid_level_missing` | YOLO 가 `liquid_level` 박스 0개 | 액체 수위 미감지 (용기 비었거나 너무 적음) |

### Category B: 위치 이탈 (Misaligned)
YOLO 가 박스를 탐지했지만, **기준 위치와 IoU < 0.3** → 제 자리에 없다고 판정

| 결함 코드 | 트리거 조건 | 의미 |
|-----------|-----------|------|
| `cap_misaligned` | cap 탐지됐지만 IoU < 0.3 | 캡 위치가 틀어지거나 삐뚤어짐 |
| `label_misaligned` | label 탐지됐지만 IoU < 0.3 | 라벨 위치가 틀어지거나 기울어짐 |
| `liquid_level_misaligned` | liquid_level 탐지됐지만 IoU < 0.3 | 충전량이 정상 범위 벗어남 (과충전/부족) |

### 정상 위치 기준 (`_reference_boxes`)
```python
self._reference_boxes = {
    "cap":          {"y_min": 0.0,  "y_max": 0.15, "x_min": 0.25, "x_max": 0.75},
    "label":        {"y_min": 0.20, "y_max": 0.75, "x_min": 0.10, "x_max": 0.90},
    "liquid_level": {"y_min": 0.60, "y_max": 0.90, "x_min": 0.15, "x_max": 0.85},
}
```
- 이미지 전체를 0~1 비율로 정규화한 "정상 위치" 영역
- 탐지된 박스와의 IoU 를 계산해 0.3 기준으로 정상/이탈 판정

---

## 3. 각 항목과 결함의 매핑

각 프레임마다 **3가지 항목**(`cap_ok`, `label_ok`, `fill_ok`) 이 0/1 로 결정된다. 이 값이 DB `assemblies` 테이블에 저장되고 MFC 화면에도 표시된다.

| 항목 | 정상 조건 (`ok=1`) | 불량 조건 (`ok=0`) |
|------|-----------------|------------------|
| **`cap_ok`** | cap 탐지됨 **AND** 위치 정상 | `cap_missing` OR `cap_misaligned` |
| **`label_ok`** | label 탐지됨 **AND** 위치 정상 | `label_missing` OR `label_misaligned` |
| **`fill_ok`** | liquid_level 탐지됨 **AND** 위치 정상 | `liquid_level_missing` OR `liquid_level_misaligned` |

**중요**: 한 항목당 결함은 missing/misaligned 중 하나만 발생. 탐지 실패 → missing, 탐지 성공 + 위치 이상 → misaligned (상호 배타).

---

## 4. 조합 시나리오 (예시)

### 예 1: 정상 병
```python
defects = []
cap_ok=1, label_ok=1, fill_ok=1
result = "OK"
```

### 예 2: 캡만 없음
```python
defects = ["cap_missing"]
cap_ok=0, label_ok=1, fill_ok=1
result = "NG"
```

### 예 3: 캡 + 라벨 불량
```python
defects = ["cap_missing", "label_misaligned"]
cap_ok=0, label_ok=0, fill_ok=1
result = "NG"
```

### 예 4: 전부 불량 (도메인 시프트 / YOLO 탐지 실패)
```python
defects = ["cap_missing", "label_misaligned", "liquid_level_misaligned"]
cap_ok=0, label_ok=0, fill_ok=0
result = "NG"
```

---

## 5. 최종 판정 로직 요약

```python
# Inferencer.py — Station2Inferencer.infer() 핵심 부분

defects = []

# Rule 1: 누락 검사 — REQUIRED - 탐지된 클래스
detected_classes = {d["class"] for d in detections}
missing = self.REQUIRED_CLASSES - detected_classes   # {cap, label, liquid_level} 중 없는 것
for m in missing:
    defects.append(f"{m}_missing")

# Rule 2: 위치 이탈 검사 — 탐지된 각 박스의 IoU
for det in detections:
    if not det.get("position_ok", True):
        defects.append(f"{det['class']}_misaligned")

# 최종 OK/NG 결정
is_ng = len(defects) > 0
result = "NG" if is_ng else "OK"

# 각 항목 OK 여부
cap_ok   = ("cap"   in detected_classes) and ("cap_missing"   not in defects) and ("cap_misaligned"   not in defects)
label_ok = ("label" in detected_classes) and ("label_missing" not in defects) and ("label_misaligned" not in defects)
fill_ok  = ("liquid_level" in detected_classes) and ("liquid_level_missing" not in defects) and ("liquid_level_misaligned" not in defects)
```

---

## 6. MainServer 에서의 저장 방식

### `inspections.defect_type` (문자열)
전체 결함 목록을 쉼표로 join 해서 저장 (v0.14.11+).
```
"cap_missing,label_misaligned,liquid_level_misaligned"
```

### `assemblies` 테이블 (Station2 상세)
각 항목별로 개별 컬럼에 0/1 저장:
- `cap_ok` (TINYINT)
- `label_ok` (TINYINT)
- `fill_ok` (TINYINT)
- `yolo_detections` (JSON) — 탐지된 박스 원본 좌표/conf 배열
- `patchcore_score` (FLOAT) — 현재 0 (PatchCore 미사용)

---

## 7. 결함 카테고리 한눈에

```
총 6가지 결함 (PatchCore 제외)

┌─────── Missing (누락) ───────┐  ┌────── Misaligned (위치이탈) ──────┐
│  cap_missing                 │  │  cap_misaligned                   │
│  label_missing               │  │  label_misaligned                 │
│  liquid_level_missing        │  │  liquid_level_misaligned          │
└──────────────────────────────┘  └───────────────────────────────────┘

한 프레임에 최대 3개 동시 발생 가능 (항목당 1개씩, missing/misaligned 중 택1)
```

---

## 8. 확장 여지 (참고 — 현재 미구현)

현재 6가지 외에 공정 요구사항에 따라 **후처리 로직 몇 줄 추가**로 확장 가능:

| 결함 코드 (예시) | 추가 판정 조건 |
|----------------|--------------|
| `cap_multiple` | cap 박스 2개 이상 탐지 (이중 캡핑) |
| `label_too_small` | label 박스 면적이 기준 비율 미만 |
| `liquid_too_low` | liquid_level 박스 y 좌표가 너무 아래 (부족) |
| `liquid_too_high` | liquid_level 박스 y 좌표가 너무 위 (과충전) |
| `cap_tilted` | cap bbox 종횡비 이상 (기울어진 캡) |

이런 세분화가 필요하면 `Inferencer.py` 의 판정 로직에 조건 추가.

---

## 9. 참고

- 이 문서는 **YOLO 단독 판정 기준**만 다룸. 향후 `station2_patchcore.ckpt` 학습 시 `label_surface_defect` (7번째 결함) 추가됨.
- 현재 환경(Domain Shift)에서 YOLO 탐지율이 낮으면 대부분 **`cap_missing` 또는 misaligned 3종** 이 연속 발생. 재학습 또는 conf_threshold 조정 필요.

---

**관련 파일**
- `AiServer/Common/Inferencer.py` — Station2Inferencer
- `MainServer/src/storage/dao.cpp` — AssemblyDao (DB 저장)
- `MainServer/sql/schema.sql` — 테이블 스키마
