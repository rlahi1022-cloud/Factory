# DB 접속 정보

## 접속 정보

| 항목 | 값 |
|------|-----|
| Host | `127.0.0.1` (로컬) / `10.10.10.130` (외부 접속 시) |
| User | `factorymanager` |
| Password | `1234` |
| Database | `Factory` |

## 접속 명령어

```
mysql -h 127.0.0.1 -u factorymanager -p Factory
```

비밀번호 입력 프롬프트가 뜨면 `1234` 입력.

## 코드에 반영

`MainServer/src/core/main.cpp`의 `DbManager` 생성자:

```cpp
DbManager db_manager(event_bus, "127.0.0.1", "factorymanager", "1234", "Factory");
```

main.cpp에서 위 접속 정보가 설정되어 있음.

## 테이블 구조

### inspections (전체 검사 결과)

| 컬럼 | 타입 | NULL | 설명 |
|------|------|------|------|
| id | INT AUTO_INCREMENT | PK | 검사 ID |
| station_id | INT | NOT NULL | 스테이션 번호 (1 또는 2) |
| bottle_id | INT | NULL | 용기 FK (AI서버 미전송, 추후 연동) |
| model_id | INT | NULL | 모델 FK (AI서버 미전송, 추후 연동) |
| timestamp | DATETIME | NOT NULL | 검사 시각 |
| result | ENUM('ok','ng') | NOT NULL | 판정 결과 |
| confidence | FLOAT | NULL | 이상 점수 (AI서버 score 필드) |
| defect_type | VARCHAR(100) | NULL | 결함 유형 (AI서버 defect 필드) |
| image_path | VARCHAR(255) | NULL | NG 이미지 저장 경로 |
| latency_ms | INT | NOT NULL | 추론 소요 시간 (ms) |

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

### bottles (용기 상태 추적)

| 테이블 | 용도 |
|--------|------|
| bottles | 용기 상태 추적 (추후 연동) |

## AI서버 필드 → DB 컬럼 매핑

### inspections (Station1/2 공통)

| AI서버 JSON 필드 | DB 컬럼 | 비고 |
|------------------|---------|------|
| station_id | station_id | |
| timestamp | timestamp | |
| result | result | "NG" → 'ng' |
| score | confidence | |
| defect | defect_type | |
| (자동 생성) | image_path | ImageStorage 저장 경로 |
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

AI서버가 bottle_id, model_id를 보내지 않으므로 NOT NULL 제약을 해제해야 합니다:

```sql
ALTER TABLE inspections
    MODIFY bottle_id INT NULL,
    MODIFY model_id  INT NULL;

ALTER TABLE assemblies
    MODIFY bottle_id INT NULL;
```
