# v0.15.1 — Hotfix: models DB INSERT 컬럼명 정합성

## 증상
학습서버(10.10.10.120) 에서 학습이 **정상 완료** 되어 메인서버로 모델 파일을
TRAIN_COMPLETE(1104) 로 전송했는데:
- 모델 바이너리는 `./storage/models/station{N}/v...pt` 에 일단 저장됨
- 직후 `ModelDao::insert` 가 **`Unknown column 'model_path' in 'INSERT INTO'`** 로 실패
- 정합성 유지 위해 저장됐던 모델 파일 **자동 롤백 삭제**
- 결국 models 테이블 비어있음 → MODEL_RELOAD_CMD(1010) 미송신 → 추론서버 더미 모드 유지

실제 로그 (2026-04-23 11:48:53):
```
🚀 [TRAIN] 학습 완료 수신 | 모델=YOLO11 버전=v20260423_114726 정확도=0.9896 파일=5471699 bytes
❌ [DB  ] ModelDao prepare 실패 | Unknown column 'model_path' in 'INSERT INTO'
🚀 [TRAIN] DB 실패 → 모델 파일 롤백 삭제 | ./storage/models/station2/v20260423_114726.pt
❌ [TRAIN] 학습 처리 실패 | db_insert_failed
```

## 원인
기획서 v0.12 ERD 와 실제 운영 DB 는 `file_path` 컬럼을 사용하는데,
v0.15.0 시점의 `schema.sql` / `dao.cpp` 는 `model_path` 를 사용하고 있었음.
두 스키마가 공존하면서 **코드가 DB 에 없는 컬럼** 으로 INSERT 시도.

| 위치 | 이전 | 현재(정본) |
|---|---|---|
| 실제 운영 DB | `file_path VARCHAR(255) NOT NULL` | (그대로) |
| 기획서 v0.12 ERD | `file_path` | (그대로) |
| `schema.sql` (v0.15.0) | ~~`model_path`~~ | **`file_path`** |
| `dao.cpp` INSERT/SELECT | ~~`model_path`~~ | **`file_path`** |
| `ModelInfo` 구조체 | (file_path 없음) | **`file_path` 멤버 추가** |
| C++ 변수명 `ev.model_path` | 유지 | 유지 (SQL 매핑만 변경) |

## 변경 내역

### MainServer/src/storage/dao.cpp
- `INSERT INTO models (..., model_path, ...)` → `INSERT INTO models (..., file_path, ...)`
- `SELECT id, station_id, model_type, version, accuracy, deployed_at, is_active` →
  `SELECT id, station_id, model_type, version, accuracy, file_path, deployed_at, is_active`
- row 인덱스 재조정 (row[5]=file_path, row[6]=deployed_at, row[7]=is_active)
- C++ 내부 변수 `ev.model_path` 는 그대로 유지 — 의미적으로 "모델 파일 경로" 동일

### MainServer/include/storage/dao.h
- `ModelDao::ModelInfo` 에 `std::string file_path;` **필드 추가만** (기존 필드/함수 유지,
  외부 소비자 gui_router 등 기존 접근 모두 유효)

### MainServer/sql/schema.sql
- `models` 테이블을 기획서 v0.12 ERD 에 맞게 재정의:
  - `model_type ENUM('PatchCore','YOLO11')`
  - `version VARCHAR(20)`
  - `accuracy FLOAT NULL`
  - `file_path VARCHAR(255) NOT NULL`
  - `trained_by INT NULL` + `FK → users(id) ON DELETE SET NULL`
- 기존 운영 DB 와 완전 일치 — 신규 배포 환경에서도 스키마 일관성 확보

### DB_README.md
- v0.15.1 정합성 수정 공지 추가

## 변경하지 않은 것 (의도적 최소 변경)
- C++ 변수명/함수명 (`ev.model_path`, `TrainCompleteEvent::model_path`) — **그대로 유지**
- JSON 필드명 (`MODEL_RELOAD_CMD` 의 `"model_path"`, `MODEL_LIST_RES` 의 기존 필드) — **그대로**
- MFC 클라이언트 측 파싱 로직 — **변경 없음** (file_path 없이도 기존처럼 동작)
- 프로토콜 번호 및 구조 — **불변**
- `gui_notifier` / `gui_router` 의 MODEL_LIST_RES JSON 생성 — **변경 없음**
  (file_path 를 클라에 노출하려면 future work 로 남김)

## Station1 / Station2 구분 (확인)
이미 코드에 구현되어 있음 — 이번 변경과 무관하게 정상 동작:
- `station_id` (1 또는 2) + `model_type` (PatchCore / YOLO11) 조합으로 구분
- `MODEL_RELOAD_CMD(1010)` 는 `station_id` 필터 + `model_type` 슬롯 라우팅
  (v0.11.0 이후 `StationRunner._handle_model_reload` 참조)
- Station2 는 YOLO + PatchCore 이중 모델 별도 INSERT/RELOAD 가능

## 적용 절차

### 메인서버 리빌드 (필수)
```bash
cd /home/lms/Desktop/Factory/MainServer/build
cmake --build . -- -j$(nproc)
./factory_main_server
```

### DB 작업 (불필요 — 기존 DB 가 이미 정본)
실제 운영 DB 는 이미 `file_path` 컬럼을 갖고 있으므로 **ALTER TABLE 필요 없음**.
신규 환경에 schema.sql 로 처음부터 생성하는 경우에만 이번 schema.sql 수정이 적용됨.

### 재학습으로 End-to-End 검증
1. MFC 클라이언트 [모델 관리] 탭 → 폴더 선택 → "재학습 실행"
2. 학습서버(10.10.10.120) 로 데이터 업로드 진행률 표시
3. 학습 완료 후 메인서버 로그에서 확인:
   ```
   🚀 [TRAIN] 학습 완료 수신 | 모델=... 버전=... 정확도=... 파일=... bytes
   🟩 [DB  ] INSERT models | id=1 스테이션=... 모델=...
   ➡️ [PUSH ] MODEL_RELOAD_CMD 전송 | station_id=... version=...
   ```
4. 추론서버(각 Station) 로그에서 모델 리로드 성공 확인
5. MFC PageStation1 "검사 설정" 라벨이 `"로딩 중..."` → 실제 모델명/버전으로 갱신
