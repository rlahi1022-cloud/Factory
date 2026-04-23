# v0.15.6 — Station2 YOLO detections 를 NG_PUSH(110) 에 포함

## 증상
PageStation2 우측 상단의 YOLO 디텍션 리스트 (클래스 / 신뢰도 / 판정) 가
**항상 비어 있음**. Station2 NG 가 초당 2~3건 수신되어도 리스트에 아무것도 표시 안 됨.

## 원인 (5단계 경로 중 하나 단절)

Station2 YOLO 의 `detections` 배열이 end-to-end 어떻게 흘러야 하는지:

| 단계 | 상태 |
|---|---|
| 1. AI `Station2Inferencer.infer()` 가 `detections` 생성 | ✅ 구현 |
| 2. `StationRunner` 가 STATION2_NG body 에 포함해 송신 | ✅ `body_dict=result_dict` |
| 3. MainServer `Router` 가 `ev.raw_json` 에 보존 | ✅ 전체 JSON 저장 |
| 4. `AssemblyDao` 가 `yolo_detections` 컬럼에 INSERT | ✅ DB 저장됨 (assemblies id 1002~ 연속) |
| **5. `gui_notifier` 가 NG_PUSH(110) JSON 에 detections 포함** | ❌ **이 단계 누락** |
| 6. MFC `OnNetNgPush` 파싱 (v0.15.0 에서 이미 구현) | ✅ `ExtractArraySize("detections")` 호출 준비됨 |
| 7. PageStation2 `m_listYolo` 에 표시 | ✅ 컬럼 3개 세팅 완료 |

### gui_notifier 가 빼먹던 필드 4종
- `detections` — YOLO 객체 탐지 결과 배열
- `cap_ok` / `label_ok` / `fill_ok` — 구조 판정 결과

---

## 수정 (최소 침습)

`MainServer/src/session/gui_notifier.cpp` 의 INSPECT_NG_PUSH(110) JSON 조립부에
4개 필드 추가. `ev.raw_json` 에서 `AssemblyDao::extract_array/int` static 유틸로
즉석 추출 (기존 DB 저장에 이미 쓰던 로직 재사용).

```diff
+ #include "storage/dao.h"   // AssemblyDao::extract_array/int 재사용

+ std::string detections_arr = (ev.station_id == 2)
+     ? AssemblyDao::extract_array(ev.raw_json, "detections") : std::string("[]");
+ int cap_ok   = (ev.station_id == 2) ? AssemblyDao::extract_int(ev.raw_json, "cap_ok")   : 0;
+ int label_ok = (ev.station_id == 2) ? AssemblyDao::extract_int(ev.raw_json, "label_ok") : 0;
+ int fill_ok  = (ev.station_id == 2) ? AssemblyDao::extract_int(ev.raw_json, "fill_ok")  : 0;

  os << "{\"protocol_no\":110"
     ... 기존 필드 ...
+    << ",\"detections\":" << (detections_arr.empty() ? "[]" : detections_arr)
+    << ",\"cap_ok\":"     << cap_ok
+    << ",\"label_ok\":"   << label_ok
+    << ",\"fill_ok\":"    << fill_ok
     << "}";
```

### Station1 안전성
`ev.station_id == 2` 조건으로 Station2 NG 일 때만 추출. Station1 NG 는 `"[]"/0`
으로 기본값 채움 → 프로토콜 변경 없음 (필드는 항상 존재), MFC 측은 빈 배열을 안전하게 무시.

---

## 영향 및 테스트

### 리빌드 필수 (MainServer)
```bash
cd /home/lms/Desktop/Factory/MainServer/build
cmake --build . -- -j$(nproc)
./factory_main_server
```

### 검증 방법
Station2 NG 발생 시 MFC PageStation2 우측 상단 YOLO 리스트에 다음 형태가 나타나야:
```
클래스       신뢰도    판정
cap          0.94     OK
label        0.87     OK
liquid_level 0.91     OK
```

또는 NG 상황 예시:
```
cap          0.82     NG   ← cap_missing 감지
label        0.88     OK
```

### AI 서버 / MFC 재빌드 불요
- AI 서버: 이미 `detections` 를 body 에 포함해 송신 중
- MFC: v0.15.0 에서 파싱 로직 이미 준비됨

---

## 프로토콜 스펙 갱신

### `INSPECT_NG_PUSH(110)` JSON 본문 (v0.15.6)

```json
{
  "protocol_no": 110,
  "id": 12345,
  "inspection_id": "station2-20260423...",
  "station_id": 2,
  "result": "NG",
  "defect_type": "cap_missing",
  "score": 0.82,
  "latency_ms": 45,
  "timestamp": "2026-04-23T15:03:40",
  "image_size": 120000,
  "heatmap_size": 810000,
  "pred_mask_size": 0,
  "detections": [                   // ← v0.15.6: Station2 YOLO 탐지
    {"class": "cap",   "conf": 0.82, "ok": false, "bbox": [x,y,w,h]},
    {"class": "label", "conf": 0.88, "ok": true},
    {"class": "liquid_level", "conf": 0.91, "ok": true}
  ],
  "cap_ok":   0,                    // ← v0.15.6
  "label_ok": 1,                    // ← v0.15.6
  "fill_ok":  1                     // ← v0.15.6
}
```

Station1 의 NG_PUSH 는 동일한 구조이지만 `detections=[]`, `cap_ok=0, label_ok=0,
fill_ok=0` 기본값으로 채워짐.

---

## 남은 개선 (선택)

- PageStation2 UI 에 cap_ok / label_ok / fill_ok 개별 상태 표시 위젯 추가
  (현재는 YOLO 리스트의 "판정" 컬럼으로 암시적 표현)
- detections 배열의 `bbox` 좌표를 MFC 에서 받아 CameraView 에 녹색/빨강 사각형 그리기
  (현재 bbox_overlay 는 서버에서 이미 합성된 이미지로 전달되므로 UI 측 그리기는 불요)
