# v0.15.4 — Station2 YOLO11 단독 확정 (PatchCore 제거)

## 설계 변경
**Station2 는 이제 YOLO11 단독** 으로 운영. 기존 "YOLO11 + PatchCore 하이브리드"
계획에서 PatchCore 가 제거됨. 조립 검사의 라벨 표면 품질은 Station1 PatchCore
(입고 검사) 에서 이미 검증되며, Station2 는 **구조 결함** (cap/label/liquid_level
누락·기울어짐) 판정에만 집중.

## 구현 원칙: 최소 침습
기존 코드가 `patchcore_model_path=""` 일 때 자동으로 YOLO 단독 동작하도록
설계되어 있어 **config 값 + UI 옵션 + 주석/문서만** 수정. 핵심 로직 변경 없음.

---

## 변경 파일

### 1. config/config.json
```diff
"station2": {
  "model_path": "./models/station2_yolo.pt",
- "patchcore_model_path": "./models/station2_patchcore.ckpt",
+ "_comment_patchcore": "v0.15.4: Station2 는 YOLO11 단독 확정. 빈 값 → 자동 skip.",
+ "patchcore_model_path": "",
```

효과: `Station2Inferencer.load_model` 이 빈 경로를 감지 →
`logger.warning("Station2 PatchCore model not found ... — surface check disabled")`
→ `self._patchcore = None` → `_run_patchcore` 는 0.0 반환 → total_score 는 YOLO 판정에만 의존.

### 2. client/Factory_UI_CL/PageModel.cpp
- **콤보박스 항목 제거**: "Station #2 — PatchCore" 삭제
  (v0.11.0 에서 추가됐던 옵션. 사용자 혼동 방지)
- `OnBtnRetrain` 의 `sel==2` 분기 제거 + 안전 폴백 주석

### 3. AiServer/Common/Inferencer.py
- `Station2Inferencer.infer()` 반환 dict 상단의 v0.15.3 주석 정정:
  - `pred_mask` / `heatmap` 이 없는 것은 **"영구적 설계 결정"** (이전 "미구현" 에서 격상)
  - v0.16 구현 계획은 폐기 명시

### 4. 문서 5종
| 파일 | 변경 |
|---|---|
| `README.md` | 시스템 구성도의 Station2 표기: "YOLO11 + PatchCore 하이브리드" → "YOLO11 단독" |
| `Client_README.md` | 5개 탭 설명 + 프로토콜 테이블에서 Station2 PatchCore 제거 |
| `AiServer_README.md` | Station2 섹션 재작성 — 단독 모델 명시 / pred_mask 영구 미생성 확정 |
| `docs/CHANGES_v0.15.4.md` | 신규 (이 문서) |
| `docs/CHANGES_v0.15.3.md` | v0.15.4 로 덮어씌워진 결정 맥락 보존 (수정 불필요) |

---

## 의도적으로 **변경하지 않은** 것

| 항목 | 유지 이유 |
|---|---|
| `Station2Inferencer._run_patchcore()` 메서드 자체 | 모델이 None 이면 조기 반환 경로 이미 존재. 제거하면 회귀 위험 |
| `MODEL_RELOAD_CMD` 의 Station2 PatchCore 슬롯 라우팅 (v0.11.0) | 외부에서 호출 안 해도 dead 경로로 존재해도 무해 |
| DB `assemblies.patchcore_score FLOAT NOT NULL` | 과거 레코드 1100+ 건 존재. 마이그레이션 불필요 |
| `config.json` 의 `yolo_conf_threshold`, `yolo_iou_threshold`, `anomaly_threshold` | 남은 Station1 anomaly / Station2 YOLO 용으로 계속 사용 |
| `AiServer/data/station2/patchcore/` 디렉토리 | 향후 실험/학습 여지 남겨 보관 |

## 운영 영향도 평가

| 경로 | 변경 전 | 변경 후 |
|---|---|---|
| Station2 NG 판정 | YOLO 결함 **또는** PatchCore 표면 이상 | YOLO 결함만 |
| `total_score` | `yolo_confidence + patchcore_score` 조합 | YOLO confidence 기준 |
| `defects` 리스트 | `anomaly` 포함 가능 | 구조 결함(cap/label/liquid_level) 중심 |
| UI 표시 | 3뷰 (원본/히트맵/pred_mask) 중 pred_mask 빈 플레이스홀더 | 동일 (의미 동일) |
| 로그 `결함=anomaly` 빈도 | 78% 였음 (v0.15.3 실측) | 자연스럽게 0 또는 급감 예상 |

**시연 리스크**: 낮음 — Station2 가 이제 "결함 유형이 명확한 것만" NG 로 판정 →
false positive 감소, 해석 가능성 증가.

## 적용 절차

### AI 서버 재시작 필수
```bash
pkill -9 -f "Station2.Station2Main"
cd /home/lms/Desktop/Factory/AiServer
python -m Station2.Station2Main
```
기동 로그에 다음이 나와야 정상:
```
Station2 PatchCore model not found:  — surface check disabled
```
(`pc_path` 가 빈 문자열이라 자연스럽게 warning 후 skip)

### MFC 클라이언트 리빌드 권장
- 콤보박스 변경 반영 위해
- 기존 설치본은 "Station #2 — PatchCore" 가 계속 표시되지만 선택해도 안전 폴백(Station1 PatchCore 로 취급)

### 메인서버 재빌드 불요
코드 변경 없음.

## 차기 정리 대상 (v0.16 후보, 시연 이후)

- `Station2Inferencer._run_patchcore` 및 관련 import 완전 제거 (dead code)
- `MODEL_RELOAD_CMD` 의 Station2 PatchCore 슬롯 라우팅 단순화
- DB `assemblies.patchcore_score` 컬럼 제거 (또는 NULL 허용)
- `AiServer/data/station2/patchcore/` 디렉토리 정책 결정
