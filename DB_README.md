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

(현재는 `127.0.0.1` / `factory` / `factory_pw` / `factory_qc`로 placeholder 상태)

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

### 기타 테이블

| 테이블 | 용도 |
|--------|------|
| users | 사용자 계정 (admin/operator/viewer) |
| models | AI 모델 버전 관리 |
| bottles | 용기 상태 추적 |

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
