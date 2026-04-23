# v0.15.3 — Station2 본격 가동 점검 (코드 변경 없음, 검증만)

## 배경
v0.15.2 리빌드 후 Station2 가 실제로 NG 를 초당 2~3건 수신하는 본격 가동 상태에
진입. **통신 경로·JSON 파싱·DB 영속화** 세 축에 잠재 위험이 있는지 전수 점검함.

결과: **치명 이슈 0건**. 코드 수정 불필요. 기능적 미완성 1건(Station2 pred_mask
미생성) 을 문서화만 함.

---

## 실측 결과 (2026-04-23 오후 기준)

### ✅ 통신/JSON 안정성 — 완벽

| 영역 | 수치 |
|---|---|
| Station2 NG 수신 건수 (오늘) | 5,000건 초과 (anomaly 4,535 + cap_missing 405 + label_misaligned 357 + liquid_level_missing 228 + label_missing 171) |
| JSON 파싱 실패 | **0건** |
| ACK 타임아웃 | **0건** |
| 재전송 (ACK 미수신) | **0건** |
| SLICED-FAILURE (비동기 persist 실패) | **0건** |
| NACK 수신 | **0건** |
| `assemblies` INSERT 연속 성공 | id 1002~1107+ (100%) |
| GUI 푸시 수신자 | 2명 지속 전달 |

**결론**: v0.15.0 통신 정합성 수정 + v0.15.1 DB 정합성 수정 + v0.15.2 JSON 파싱
하드닝(백슬래시 카운팅 + NaN/Inf 차단) 의 누적 효과로 **실환경에서 무결성
100% 달성**.

### ✅ JSON 파서 현황 (재확인)

| 파서 | 상태 |
|---|---|
| MFC `CPacketBuilder::ExtractString` | 백슬래시 카운팅 적용 (v0.15.0) |
| MFC `CPacketBuilder::ExtractDouble` | `_finite` NaN/Inf 차단 (v0.15.2) |
| MainServer `GuiRouter::extract_str` | 백슬래시 카운팅 적용 (v0.15.2) |
| MainServer `Router::extract_str` | 단순 파싱 — **신뢰된 내부 경로**(AI↔Main)라 escape 유입 가능성 매우 낮음. 현재 안전 |
| MainServer `AssemblyDao::extract_int/double` | 단순 파싱 — `ev.raw_json` 재파싱이며 Router 에서 1차 검증된 JSON. 안전 |

v0.15.2 에서 지적한 "3곳 통합" 은 각 호출 경로의 신뢰 모델이 달라 **지금 합치면 회귀 위험**.
통합 작업은 차기 릴리스로 보류 유지.

---

## ⚠️ 기능적 미완성 1건: Station2 pred_mask 누락

### 증상
로그의 Station2 NG 푸시 크기 패턴:
```
NG 푸시 | 스테이션=2 (원본=67,159  히트맵=810,020  마스크=0)
NG 푸시 | 스테이션=2 (원본=120,399 히트맵=1,213,960 마스크=0)
...
```
**마스크가 항상 0 바이트**.

### 원인
`Station2Inferencer.infer()` 의 반환 dict 에 `pred_mask` 키가 **없음**
(`AiServer/Common/Inferencer.py:743-755`).

| Inferencer | 반환 키 |
|---|---|
| Station1 | `result, score, defect, heatmap, pred_mask, anomaly_map, ...` ✅ |
| Station2 | `result, score, defect, defects, detections, patchcore_score, cap_ok, label_ok, fill_ok, yolo_detections, bbox_overlay` — **`pred_mask` 없음** |

### 영향 범위
- ❌ **통신 에러 아님** — `pred_mask_size=0` 은 프로토콜상 허용 (빈 바이너리로 전달)
- ❌ **크래시 아님** — MFC `CameraView::SetImage(empty_vector)` 가 플레이스홀더로 처리 (v0.14.7 race 방지 로직 포함)
- ⚠️ **DB 영향 없음** — `inspections.pred_mask_path` 가 빈 문자열 → NULL 로 저장 (정상 동작)
- ⚠️ **UI 기능 미완성** — PageStation2 의 Pred Mask 뷰가 항상 비어있는 플레이스홀더 상태
- ⚠️ **저장소 파일 없음** — `./storage/station2/YYYYMMDD/*_mask.png` 파일 미생성

### 현재 상태 판정: **안전한 미구현**
통신/DB/UI 모든 경로가 "빈 pred_mask" 를 정상적으로 감당. 시연에 영향 없음.

### 향후 구현 시 작업 범위 (v0.16 후보)
Station2Inferencer 에 PatchCore 표면 이상 영역을 `pred_mask` 로 반환하는 로직 추가:
1. `_run_patchcore(label_roi)` 가 이미 PatchCore 점수를 계산 중
2. 해당 내부 호출에서 `anomaly_map` / `pred_mask` 도 가져와 반환 dict 에 포함
3. StationRunner 는 이미 `pred_mask` 키가 있으면 PNG 인코딩하도록 되어있음 (변경 불필요)
4. 영향 파일: `AiServer/Common/Inferencer.py` 의 Station2 섹션만 수정

---

## ℹ️ 결함 분포 관찰

```
anomaly              4,535건 (77.8%)
cap_missing            405건 (7.0%)
label_misaligned       357건 (6.1%)
liquid_level_missing   228건 (3.9%)
label_missing          171건 (2.9%)
```

`anomaly` 가 압도적 — PatchCore 가 "불분명한 이상" 으로 판정하는 빈도 높음.
**AI 모델 튜닝 영역** (임계값, 학습 데이터셋 다양성) 이지 통신/코드 문제 아님.

Station2 YOLO 가 구조 결함(cap/label/liquid_level) 을 22% 정도로 낮게 분류 중.
모델 성능 개선은 별도 작업 (재학습 시 데이터셋 추가 등).

---

## ✅ v0.15.2 신규 기능 검증

| 항목 | 상태 |
|---|---|
| `IDC_BTN_S1_START/STOP`, `IDC_BTN_S2_START/STOP` rc 정의 후 빌드 | — 사용자 리빌드 후 확인 필요 |
| DEFECT/REWORK 버튼 복원 (v0.15.2.1) | ✅ 로그에 `OnBtnDefect/Rework` 안내 팝업 경로 유지 |
| INSPECT_CONTROL_REQ(160) 송신 이력 | 오늘 로그에 없음 — 사용자가 아직 Start/Stop 클릭 안 한 상태 |

리빌드 후 Start/Stop 버튼으로 공정 독립 제어가 되는지 현장 확인 필요.

---

## 문서 수정

- `docs/CHANGES_v0.15.3.md` 신규 (이 문서)
- `AiServer_README.md` 에 "Station2 pred_mask 미생성 (v0.16 후보)" 섹션 추가

---

## 다음 세션 권장 작업 (시연 후)

1. **Station2 `pred_mask` 생성 구현** — Station1 과 동일한 UI 완성도 확보 (20분)
2. `Router::extract_str` / `AssemblyDao::extract_int` 를 `json_safety.h` 로 통합 (1~2시간)
3. `inspections.inspection_id` UNIQUE 컬럼 추가 (end-to-end 추적 + 중복 방지)
4. `models` UNIQUE (station_id, model_type, version) + `is_active=0` 자동 전환 로직
5. logrotate 설정으로 `./storage/station*/*.jpg` 장기 보관 정리

이것들은 모두 **시연 이후** 여유 시간에 정리하면 되는 P2/P3 수준 작업.
