# DB 접속 정보

## 접속 정보

DB 접속 정보는 **`config/config.json`의 `database` 섹션**에서 관리됩니다 (v0.7.0+).

```json
"database": {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "factorymanager",
    "password": "1234",
    "schema": "Factory",
    "pool_size": 4,
    "acquire_timeout_sec": 5
}
```

| 항목 | 기본값 | 설명 |
|------|--------|------|
| Host | `127.0.0.1` | MariaDB 호스트 (로컬) |
| Port | `3306` | MariaDB 포트 |
| User | `factorymanager` | 접속 계정 |
| Password | `1234` | 접속 비밀번호 (⚠️ 운영 시 변경 필수) |
| Database | `Factory` | 스키마명 |
| Pool Size | `4` | ConnectionPool 크기 |
| Acquire Timeout | `5초` | 풀 대기 타임아웃 |

## 접속 명령어 (수동)

```
mysql -h 127.0.0.1 -u factorymanager -p Factory
```

비밀번호 입력 프롬프트가 뜨면 `1234` 입력.

## 코드에 반영 (자동 로드)

`MainServer/src/core/main.cpp`:

```cpp
auto& cfg = Config::instance();
cfg.load("../../config/config.json");

std::string db_host     = cfg.get_str("database.host");
std::string db_user     = cfg.get_str("database.user");
std::string db_password = cfg.get_str("database.password");
std::string db_schema   = cfg.get_str("database.schema");
int         pool_size   = cfg.get_int("database.pool_size", 4);

ConnectionPool db_pool(db_host, db_user, db_password, db_schema, 3306, pool_size);
```

**하드코딩 제거됨** — config.json에서 읽어옴.

## 테이블 구조

### inspections (전체 검사 결과)

| 컬럼 | 타입 | NULL | 설명 |
|------|------|------|------|
| id | INT AUTO_INCREMENT | PK | 검사 ID |
| station_id | INT | NOT NULL | 스테이션 번호 (1 또는 2) |
| bottle_id | INT | **NULL 허용** ✅ | 용기 FK (v0.7.0+ NULL 허용) |
| model_id | INT | **NULL 허용** ✅ | 모델 FK (v0.7.0+ NULL 허용) |
| timestamp | DATETIME | NOT NULL | 검사 시각 (ISO8601 자동 변환) |
| result | ENUM('ok','ng') | NOT NULL | 판정 결과 (소문자) |
| confidence | FLOAT | NULL | 이상 점수 (AI서버 score 필드) |
| defect_type | VARCHAR(100) | NULL | 결함 유형 (AI서버 defect 필드) |
| image_path | VARCHAR(255) | NULL | NG 원본 이미지 저장 경로 |
| heatmap_path | VARCHAR(255) | NULL | NG Anomaly Map(원본+히트맵) 이미지 경로 (v0.9.0+) |
| pred_mask_path | VARCHAR(255) | NULL | NG Pred Mask(원본+윤곽선) 이미지 경로 (v0.9.0+) |
| latency_ms | INT | NOT NULL | 추론 소요 시간 (ms) |

**⚠️ timestamp 형식 자동 변환 (v0.8.0+):**
- AI서버 전송: ISO8601 `"2026-04-20T12:34:56.789+00:00"`
- MainServer DAO: MySQL DATETIME으로 자동 변환 `"2026-04-20 12:34:56"`
- 구현: `iso8601_to_mysql()` in [dao.cpp](MainServer/src/storage/dao.cpp)

### assemblies (조립 검사 상세 — Station2 전용)

| 컬럼 | 타입 | NULL | 설명 |
|------|------|------|------|
| id | INT AUTO_INCREMENT | PK | 조립 검사 ID |
| inspection_id | INT | NOT NULL | inspections FK (1:1) |
| bottle_id | INT | NULL | 용기 FK (AI서버 미전송, 추후 연동) |
| cap_ok | TINYINT(1) | NOT NULL | 캡 체결 정상 여부 |
| label_ok | TINYINT(1) | NOT NULL | 라벨 부착 정상 여부 |
| fill_ok | TINYINT(1) | NOT NULL | 충전량 정상 여부 |
| yolo_detections | JSON | NOT NULL | YOLO11 탐지 결과 |
| patchcore_score | FLOAT | NOT NULL | PatchCore 이상 점수 |
| timestamp | DATETIME | NOT NULL | 검사 시각 |

#### users (사용자 계정)

| 컬럼 | 타입 | NULL | 설명 |
|------|------|------|------|
| id | INT AUTO_INCREMENT | PK | 사용자 ID |
| employee_id | VARCHAR(20) | NOT NULL | 사원 ID |
| username | VARCHAR(50) UNIQUE | NOT NULL | 로그인 아이디 |
| password_hash | VARCHAR(255) | NOT NULL | 비밀번호 (bcrypt 해시, $2b$12$...) |
| role | VARCHAR(20) | NOT NULL | 권한 (Admin/Operator/Viewer) |
| created_at | DATETIME | DEFAULT NOW() | 가입 시각 |
| last_login_at | DATETIME | NULL | 마지막 로그인 시각 |

클라이언트 LOGIN_REQ(100) → DB 조회 + bcrypt 검증, REGISTER_REQ(104) → bcrypt 해시 후 DB INSERT.

초기 관리자 계정 등록 (bcrypt 해시):
```sql
-- 서버에서 회원가입 기능으로 등록하거나, 아래 SQL로 평문 INSERT 후
-- 첫 로그인 시 서버가 bcrypt로 자동 검증
INSERT INTO users (employee_id, username, password_hash, role)
VALUES ('EMP-001', 'admin01', '$2b$12$...해시값...', 'Admin');
```

### models (AI 모델 버전 관리)

| 컬럼 | 타입 | NULL | 설명 |
|------|------|------|------|
| id | INT AUTO_INCREMENT | PK | 모델 ID |
| station_id | INT | NOT NULL | 스테이션 번호 (1 또는 2) |
| model_type | VARCHAR(50) | NOT NULL | 모델 종류 (PatchCore / YOLO11) |
| version | VARCHAR(50) | NOT NULL | 모델 버전 |
| accuracy | DOUBLE | NOT NULL | 정확도 (AUROC 또는 mAP50) |
| model_path | VARCHAR(255) | NULL | 모델 파일 경로 |
| deployed_at | DATETIME | DEFAULT NOW() | 배포 시각 |
| is_active | TINYINT(1) | DEFAULT 1 | 활성 여부 |

학습서버 TRAIN_COMPLETE(1104) 수신 시 DbManager가 자동 INSERT.

> ✅ **v0.15.1 정합성 수정**: 기획서 v0.12 ERD 가 정본. 실제 운영 DB 와 `schema.sql` /
> `dao.cpp` 모두 **`file_path`** 컬럼명, `ENUM('PatchCore','YOLO11')`, `trained_by FK → users.id`
> 구조로 통일됨. 이전에 `dao.cpp` 가 `model_path` 로 INSERT 시도하다가 "Unknown column"
> 에러로 학습 완료 배포 파이프라인이 전부 실패하던 문제를 수정함.

### bottles (용기 상태 추적)

| 테이블 | 용도 |
|--------|------|
| bottles | 용기 상태 추적 (추후 연동) |

> ⚠️ **기획서 v0.12 ERD 대비 컬럼 구성 차이**:
> - 기획서: `bottle_type VARCHAR(50)`, `batch_no VARCHAR(50)`, `status ENUM('pending','ok','ng')`
> - 실제 `schema.sql`: `code VARCHAR(50) UNIQUE`, `status VARCHAR(20)`, `updated_at` 추가
>
> 현재 DAO 가 `bottles` 에 INSERT 하지 않으므로 운영상 영향 없음. 기획서 원안대로 복원할지
> 현 스키마를 기준으로 기획서를 업데이트할지는 추후 결정 필요.

## AI서버 필드 → DB 컬럼 매핑

### inspections (Station1/2 공통)

| AI서버 JSON 필드 | DB 컬럼 | 비고 |
|------------------|---------|------|
| station_id | station_id | |
| timestamp | timestamp | |
| result | result | "NG" → 'ng' |
| score | confidence | |
| defect | defect_type | |
| (자동 생성) | image_path | ImageStorage 저장 경로 (원본 JPEG) |
| (자동 생성) | heatmap_path | ImageStorage 저장 경로 (원본+히트맵 PNG, v0.9.0+) |
| (자동 생성) | pred_mask_path | ImageStorage 저장 경로 (원본+마스크 PNG, v0.9.0+) |
| latency_ms | latency_ms | |
| — | bottle_id | AI서버 미전송 (NULL) |
| — | model_id | AI서버 미전송 (NULL) |

### assemblies (Station2 전용)

| AI서버 JSON 필드 | DB 컬럼 |
|------------------|---------|
| cap_ok | cap_ok |
| label_ok | label_ok |
| fill_ok | fill_ok |
| detections | yolo_detections |
| patchcore_score | patchcore_score |

## 필요한 ALTER (bottle_id, model_id NULL 허용)

AI서버가 bottle_id, model_id를 보내지 않으므로 NOT NULL 제약을 해제해야 합니다.

**v0.7.0부터 기본 스키마에서 이미 NULL 허용**. 구버전 DB 마이그레이션 시에만 실행:

```sql
ALTER TABLE inspections
    MODIFY bottle_id INT NULL,
    MODIFY model_id  INT NULL;

ALTER TABLE assemblies
    MODIFY bottle_id INT NULL;
```

## v0.9.0 마이그레이션 — 시각화 이미지 경로 컬럼 추가

AI 추론서버가 NG 결과 전송 시 **3장 이미지**(원본 + Anomaly Map + Pred Mask)를 보내므로,
MainServer에서 저장할 경로 컬럼 2개를 추가해야 합니다.

```sql
ALTER TABLE inspections
    ADD COLUMN heatmap_path    VARCHAR(255) NULL AFTER image_path,
    ADD COLUMN pred_mask_path  VARCHAR(255) NULL AFTER heatmap_path;
```

**배경**:
- STATION1_NG(1000) / STATION2_NG(1002) 패킷이 `image_size`, `heatmap_size`, `pred_mask_size` 3개 필드를 포함
- 패킷 구조: `[header] + [JSON] + [원본 JPEG] + [히트맵 PNG] + [마스크 PNG]`
- 크기가 0인 이미지는 생략 (하위호환)

**MainServer 대응 필요**:
- `dao.cpp`의 INSERT 쿼리에 `heatmap_path`, `pred_mask_path` 컬럼 추가
- `ImageStorage`에서 3장을 각각 다른 경로로 저장 (예: `_heatmap.png`, `_mask.png` suffix)
- MFC 클라이언트 "Anomaly Map" / "Pred Mask" 영역에서 해당 경로의 이미지 로드

## ConnectionPool (v0.7.0+)

MainServer는 **4개 DB 커넥션을 풀**로 관리하여 재사용합니다.

### 특징
- **풀 크기**: `config.json`의 `database.pool_size` (기본 4)
- **Acquire 타임아웃**: 5초 (전부 사용 중일 때 최대 5초 대기)
- **자동 재연결**: mysql_ping 실패 시 재연결 시도
- **Double-close 방지**: 재연결 실패 시 old 포인터를 안전하게 교체
- **RAII 래퍼**: `PooledConnection` 사용 — 스코프 벗어나면 자동 반납

### 사용 예 (C++)
```cpp
PooledConnection conn(pool_);    // 획득 (또는 5초 대기)
if (!conn.get()) return -1;      // 타임아웃 시 null
// ... mysql_query 등 사용
// ← 스코프 종료 시 자동 release
```

## 비밀번호 해시 규칙 (중요)

### bcrypt만 허용
- `password_hash` 필드에는 **반드시 bcrypt 해시** (`$2b$12$...` 60자)
- 평문으로 INSERT하면 `PasswordHash::verify()` 실패 → 로그인 거부

### bcrypt 해시 생성
```bash
# Python
python3 -c "import crypt; print(crypt.crypt('password', crypt.mksalt(crypt.METHOD_BLOWFISH)))"

# 결과 예시:
# $2b$12$0AzNHhCkFvNKZ/2rEDt8auON.dOx.0BLDVUZlfZBwuDH6BTvIZk1W
```

### 권장 방법
MFC 클라이언트의 **회원가입** 기능 사용 (서버가 자동 해싱).

## DB 스키마 생성 (처음 설정 시)

```sql
CREATE DATABASE IF NOT EXISTS Factory DEFAULT CHARACTER SET utf8mb4;
USE Factory;

-- inspections
CREATE TABLE IF NOT EXISTS inspections (
    id INT AUTO_INCREMENT PRIMARY KEY,
    station_id INT NOT NULL,
    bottle_id INT NULL,
    model_id INT NULL,
    timestamp DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    result ENUM('ok','ng') NOT NULL,
    confidence FLOAT,
    defect_type VARCHAR(100),
    image_path VARCHAR(255),
    heatmap_path VARCHAR(255),
    pred_mask_path VARCHAR(255),
    latency_ms INT NOT NULL,
    INDEX(station_id), INDEX(result), INDEX(timestamp)
);

-- 나머지 테이블 스키마도 유사...
```
