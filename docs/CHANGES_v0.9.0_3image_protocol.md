# v0.9.0 — NG 3장 이미지 전송 프로토콜 적용

**날짜**: 2026-04-21
**범위**: AI추론서버 / MainServer(C++) / DB
**미완료**: MFC 클라이언트 3분할 UI 수정

---

## 1. 개요

AI 추론 서버가 NG 판정 시 **원본 JPEG 1장**만 보내던 구조를,
**원본 + Anomaly Map + Pred Mask 총 3장**을 보내도록 확장.
MFC 클라이언트의 3분할 영역(Original / Anomaly Map / Pred Mask)에 바로 표시 가능.

### 와이어 포맷 (변경 후)

```
[4바이트 JSON 길이(BE)] + [JSON]
  + [원본 JPEG]       (image_size 바이트)
  + [히트맵 PNG]      (heatmap_size 바이트)
  + [Pred Mask PNG]   (pred_mask_size 바이트)
```

JSON 예:
```json
{
  "protocol_no": 1000,
  "inspection_id": "station1-20260421-000123",
  "station_id": 1,
  "result": "NG",
  "score": 0.87,
  "image_size": 50000,
  "heatmap_size": 30000,
  "pred_mask_size": 15000,
  ...
}
```

**하위호환**: `heatmap_size` / `pred_mask_size`가 없거나 0이면 해당 이미지 생략.

---

## 2. AI 추론 서버 (Python)

| 파일 | 변경 내용 |
|------|-----------|
| `AiServer/Common/Packet.py` | `PacketBuilder.build_packet()`에 `heatmap_bytes`, `pred_mask_bytes` 인자 추가. JSON에 3개 size 필드 주입. |
| `AiServer/Common/Visualizer.py` **(NEW)** | `make_heatmap_overlay()` / `make_pred_mask_overlay()` / `encode_image()` — OpenCV 기반 시각화 유틸 |
| `AiServer/Common/StationRunner.py` | `ResultItem` slots에 `heatmap_bytes`, `pred_mask_bytes` 추가. NG 판정 시 시각화 2장 생성해 패킷에 포함. |
| `AiServer/Common/Protocol.py` | `STATION1_NG` / `STATION2_NG` 주석에 3장 이미지 구조 문서화 |

### Inferencer가 반환해야 하는 추가 필드
- `raw_anomaly_map`: 정규화 전 float 배열 (히트맵 생성용)
- `pred_mask`: 이진 마스크 (윤곽선 생성용)

---

## 3. MainServer (C++)

### 변경 파일

| 파일 | 변경 내용 |
|------|-----------|
| `include/core/event_types.h` | `PacketReceivedEvent` / `InspectionEvent`에 `heatmap_bytes`, `pred_mask_bytes` 필드 추가 |
| `include/core/tcp_listener.h` | `recv_one_packet()` 시그니처 확장 (3개 `out` 벡터), 주석 업데이트 |
| `src/core/tcp_listener.cpp` | `extract_size_field()` 헬퍼 추가. 3개 size 파싱 후 순서대로 수신. 각 이미지 50MB 상한. |
| `src/handler/router.cpp` | NG 이벤트 변환 시 `heatmap_bytes` / `pred_mask_bytes`를 `InspectionEvent`에 복사 |
| `include/service/inspection_service.h` | `InspectionResult`에 `heatmap_path` / `pred_mask_path` 추가. `save_image()` → `save_blob()` 일반화 |
| `src/service/inspection_service.cpp` | 처리 순서 변경: **저장 → INSERT**. 3장 각각 저장 후 세 경로를 DAO에 전달 |
| `include/storage/dao.h` | `InspectionDao::insert()`에 3개 경로 파라미터 추가 (기본값 `""`, 하위호환) |
| `src/storage/dao.cpp` | INSERT SQL에 `heatmap_path` / `pred_mask_path` 컬럼 추가. 바인딩 7 → 9개 |
| `src/session/gui_notifier.cpp` | MFC 푸시 JSON에 3개 size 포함. 3장을 이어붙인 하나의 블록으로 `broadcast_with_binary` 호출 |

### 파일명 규칙

```
./storage/station{N}/{YYYYMMDD}/ng_{epoch_ms}_original.jpg
./storage/station{N}/{YYYYMMDD}/ng_{epoch_ms}_heatmap.png
./storage/station{N}/{YYYYMMDD}/ng_{epoch_ms}_mask.png
```

### 동작 흐름

```
AI서버 → [JSON(3 size) + 원본 + 히트맵 + 마스크]
         ↓ TcpListener.recv_one_packet (3개 vector 수신)
         ↓ Router → InspectionEvent(3 bytes 필드)
         ↓ StationHandler → InspectionService.process
            ├─ save_blob × 3 → 3개 파일 저장
            └─ InspectionDao.insert(ev, img, heatmap, mask)
                → DB inspections INSERT (9컬럼)
         ↓ GUI_PUSH_REQUESTED → GuiNotifier
            → MFC에 [JSON + 3장 연결 블록] 전송
```

---

## 4. DB 스키마 (MariaDB)

### 적용된 ALTER (완료)

```sql
ALTER TABLE inspections
    ADD COLUMN heatmap_path    VARCHAR(255) NULL AFTER image_path,
    ADD COLUMN pred_mask_path  VARCHAR(255) NULL AFTER heatmap_path;
```

### inspections 테이블 (v0.9.0)

| 컬럼 | 타입 | NULL | 비고 |
|------|------|------|------|
| id | INT AUTO_INCREMENT | PK | |
| station_id | INT | NOT NULL | |
| bottle_id | INT | NULL | |
| model_id | INT | NULL | |
| timestamp | DATETIME | NOT NULL | |
| result | ENUM('ok','ng') | NOT NULL | |
| confidence | FLOAT | NULL | |
| defect_type | VARCHAR(100) | NULL | |
| image_path | VARCHAR(255) | NULL | 원본 JPEG |
| **heatmap_path** | **VARCHAR(255)** | **NULL** | **Anomaly Map PNG (v0.9.0+)** |
| **pred_mask_path** | **VARCHAR(255)** | **NULL** | **Pred Mask PNG (v0.9.0+)** |
| latency_ms | INT | NOT NULL | |

### 문서

- `DB_README.md` — 스키마 표 / CREATE TABLE / 마이그레이션 섹션 업데이트

---

## 5. 하위호환

| 상황 | 동작 |
|------|------|
| 구버전 AI서버(필드 누락) | `extract_size_field()`가 0 반환 → skip → DB는 NULL |
| 구버전 MainServer + 신버전 AI서버 | MainServer가 `heatmap_size`/`pred_mask_size` 모르므로 파싱 실패 → 재빌드 필수 |
| DB ALTER 미적용 | INSERT SQL 컬럼명 불일치로 실패 → ALTER 선행 필수 |

---

## 6. 남은 작업

### MFC 클라이언트 (필수)
- **UI 3분할 영역**: Original / Anomaly Map / Pred Mask
- **수신 파싱**: `INSPECT_NG_PUSH(110)` JSON에서 `image_size` / `heatmap_size` / `pred_mask_size` 읽고 오프셋 계산해 3장 분리
- **이력 화면**: DB의 `heatmap_path` / `pred_mask_path` 로드 지원

### 이력 조회 API (선택)
- `StatsDao::InspectionRecord`에 `heatmap_path` / `pred_mask_path` 필드 추가
- `get_history()` SELECT 확장
- `INSPECT_HISTORY_RES(115)` JSON에 추가

### 운영
- `./storage/` 디스크 용량 모니터링 — 1건당 파일 3장으로 증가
- 오래된 이미지 자동 삭제 정책 검토

---

## 7. 테스트 체크리스트

- [ ] AI서버 NG 1건 발생 → 3개 파일 생성 확인 (`ls storage/station1/*/ng_*`)
- [ ] DB `SELECT image_path, heatmap_path, pred_mask_path FROM inspections ORDER BY id DESC LIMIT 1`
- [ ] 구버전 AI서버 연결 → `heatmap_path`/`pred_mask_path` NULL 기록 확인
- [ ] MFC 클라이언트 3분할 표시 (UI 수정 후)
- [ ] 디스크 여유 공간 부족 시 fail-safe 동작 확인
