# Factory — CNN 기반 페트병 공정 모니터링 시스템

## 프로젝트 개요

CNN 기반 머신비전 기술을 활용하여
페트병 생산 공정에서 발생하는 불량을 실시간으로 탐지하고,

Edge AI 추론 서버와 이벤트 기반 Main Server를 통해
데이터를 수집·처리·저장·시각화하는 공정 모니터링 시스템이다.

## 프로젝트 정보
- 팀원: 김혜윤, 심동주, 정지훈, 임완, 오인효
- 기간: 2026.04.13 ~ 2026.04.25 (12일)
- 장소: 광주인력개발원

---

## 시스템 구성

```
Camera → AI Server → Main Server → DB → MFC Client
                         ↑                    ↓
                   HealthChecker        SessionManager
                   (5초 주기 감시)     (GUI 세션 관리)
```

### 구성 요소

* **AiServer (Python 3.10+)**

  * asyncio.Queue 기반 비동기 파이프라인
  * Station1: PatchCore 이상탐지 (입고 검사)
  * Station2: YOLO11 + PatchCore 하이브리드 (조립 검사)
  * NG 데이터만 서버 전송 (네트워크 최적화)
  * ACK 기반 재전송 (1초 타임아웃, 최대 3회)

* **MainServer (C++17)**

  * EventBus (워커 풀 4스레드 병렬 처리) + Service 레이어 아키텍처
  * Handler → Service(검증+트랜잭션) → DAO(DB) 계층 분리
  * ConnectionPool (4개 DB 커넥션 공유)
  * bcrypt 비밀번호 해시 (glibc crypt_r)
  * 추론서버 수신 (포트 9000) + GUI 클라이언트 수신 (포트 9010)
  * 모델 바이너리 TCP 중계 (학습서버 → 메인서버 → 추론서버)
  * SessionManager를 통한 MFC 클라이언트 broadcast

* **Training Server (Python)**

  * TCP 서버 (포트 9100)
  * PatchCore / YOLO11 학습
  * 학습 진행률 송신 + 완료/실패 알림

* **MFC Client**

  * 실시간 검사 결과 모니터링
  * 통계 및 이력 조회 UI

* **Database (MariaDB)**

  * inspections / assemblies / bottles / models / users 테이블
  * inspection_id 기반 end-to-end 추적

---

## 전체 데이터 흐름

1. 카메라에서 이미지 캡처
2. AI 서버에서 OK / NG 판정
3. 모든 검사 결과 → `INSPECT_META(1006)` 전송 (fire-and-forget)
4. NG 발생 시 → `STATION_NG(1000/1002)` 전송 (ACK 필수)
5. Main Server에서 DB INSERT + NG 이미지 저장
6. ACK 회신 → AI 서버
7. SessionManager를 통해 MFC 클라이언트에 실시간 push

---

## 핵심 설계 특징

### 아키텍처
* **3계층 아키텍처**: Handler → Service → DAO (SRP 적용)
* **이벤트 기반**: EventBus + Worker Pool 4스레드 병렬 처리
* **Hub-and-Spoke**: MainServer가 모든 통신의 중앙 허브
* **관심사 분리**: 통신/로직/저장/세션/보안 계층 완전 분리

### 신뢰성
* **TCP Keepalive 공격적 튜닝**: 60초 유휴 → 10초 × 3회 probe → 90초 내 좀비 감지
* **지수 백오프 재시도**: MODEL_RELOAD 1 → 5 → 30 → 120 → 300초 (5회 시도)
* **Atomic File Write**: 임시파일 → fsync → rename (부분 파일 방지)
* **Snapshot-then-Release**: broadcast 시 mutex 내 스냅샷만, send는 락 밖에서
* **ConnectionPool 재연결**: mysql_ping 실패 시 자동 복구 + double-close 방지
* **UI 연결 상태 실시간 동기화**: 1초 타이머로 소켓 상태 검증

### 데이터 흐름
* NG 중심 전송 구조 (트래픽 최적화)
* inspection_id 기반 end-to-end 추적
* ACK / 재전송 기반 데이터 신뢰성 확보
* **v0.12.0: 검증 즉시 ACK → 백그라운드 영속화** (INSPECTION_VALIDATED 이벤트)
* 모델 바이너리 TCP 전송 (학습서버 → 메인서버 → 추론서버)
* asyncio.Queue 기반 비동기 처리 (Backpressure 대응)

### 보안 (Defense in Depth)
* MariaDB Prepared Statement (SQL injection 원천 차단)
* IP 화이트리스트 (내부망만 허용)
* Input Validation + Output Escaping (JSON injection 방지)
* bcrypt + Salt (/dev/urandom 기반 암호학적 난수)
* Path Traversal 차단 (version 문자열 검증)

상세 설계 원칙은 [ARCHITECTURE_PRINCIPLES.txt](ARCHITECTURE_PRINCIPLES.txt) 참고

---

## 보안 강화 내역

### 적용된 방어

* **SQL Injection 차단**: 모든 DAO prepared statement 사용
* **IP 화이트리스트**: AI 서버 포트 9000에 내부망(10.x, 192.168.x, 172.16~31.x)만 허용
* **입력 크기 제한**: JSON 64KB, 이미지 50MB, 모델 500MB 상한
* **partial send 재시도**: `tcp_utils.h`의 `send_all` / `send_json_frame` 사용
* **동시 접속 제한**: AI 서버 10개, GUI 세션 20개
* **동시 학습 방지**: `GuiService`에 mutex 기반 `is_training_` 플래그
* **동시 로그인 차단**: 같은 username 기존 세션 `force_close`
* **bcrypt 비밀번호 해시**: `/dev/urandom` 실패 시 `random_device` fallback + 엔트로피 검증
* **모델 파일 무결성**: 저장 후 크기 검증 + atomic rename
* **JSON escape**: 모든 사용자 입력을 이스케이프하여 JSON injection 방지
* **Path traversal 차단**: 모델 version 문자열에서 `../` 검증
* **디스크 여유 확인**: 이미지 저장 전 100MB 이상 여유 확인
* **recv 타임아웃**: AI/GUI 클라이언트 소켓에 30초 타임아웃 (slow loris 방어)
* **ConnectionPool 재연결**: mysql_ping 실패 시 안전한 재연결 + double-close 방지
* **로그 파일**: `logs/YYYY-MM-DD.log` 자동 로테이션 (stdout + 파일 tee)

### 추가 신뢰성 개선 (v0.8.0)

* **로그인 시 서버 상태 초기 동기화**: 접속 직후 `HEALTH_PUSH(170)` 자동 전송 → UI LED 즉시 반영
* **UI 연결 상태 실시간 체크**: IDT_STATUSBAR 1초 타이머가 실제 소켓 상태 검증 → silent drop 자동 감지
* **RecvLoop 에러 감지 시 WM_NET_DISCONNECTED 발송**: 네트워크 끊김 시 UI 자동 갱신
* **HealthChecker 동적 주기**: 모든 서버 정상 → 30초 주기, 장애 발생 → 5초 주기 (로그 노이즈 감소)
* **TRAIN_COMPLETE station_id/model_type 필수 포함**: DB INSERT 실패 방지 (이전 버그 수정)
* **ISO8601 → MySQL DATETIME 자동 변환**: DAO에서 timestamp 파싱 처리
* **DB 스키마 개선**: `bottle_id`, `model_id` NULL 허용 (AI 시스템 설계상)

### v0.11.0 — 학습·배포 파이프라인 재설계 (이중모델/멀티호스트)

* **학습서버 주소 분리**: `config.json` 에 `network.training_server_host` 키 추가 —
  학습서버를 메인서버와 별도 PC(기본 10.10.10.120)에 배치 가능.
  환경변수 `TRAIN_HOST` 는 기존대로 오버라이드 우선순위 유지.
* **MODEL_RELOAD_CMD (1010) JSON 에 `model_type` 필드 추가**:
  Station2 이중모델(YOLO + PatchCore) 재학습 시 교체할 슬롯을 명시적으로 전달.
* **추론서버 수신측 필터 (`StationRunner._handle_model_reload`)**:
  - `station_id` 불일치 시 조용히 무시 (브로드캐스트 오배송 방지)
  - `model_type` 기반 슬롯 라우팅:
    `"YOLO11"` → `config.model_path`, `"PatchCore"` → `config.patchcore_model_path`
  - Station2 PatchCore 재학습 완료 시 YOLO 슬롯 덮어쓰기 버그 해결
* **클라이언트 UI (`CPageModel`) — Station2 PatchCore 항목 추가**:
  콤보박스에 "Station #2 — PatchCore" 옵션 추가, YOLO11 과 독립적으로 재학습 요청 가능.
* **HealthChecker 동적 서버 감지**: `ConnectionRegistry` 에 `server_type` 필드 추가.
  Router 가 수신 패킷(`station_id`/TRAIN_*/HEALTH_PONG) 으로 server_type 자동 태깅 →
  HealthChecker 가 IP 하드코딩 대신 server_type 으로 매칭. `config.json` 의
  `health_check.targets.ip` 를 비워두면 배포 PC 가 변경되어도 설정 수정 없이 자동 감지.
  로그에는 실제 접속 IP 가 표시된다.
* **Pylon 카메라 실제 연동** (`Common/PylonCamera.py` 신규):
  Basler 카메라를 `StationRunner._run_grab_producer` 에서 실제로 grab. pypylon 미설치
  또는 카메라 미연결 시 is_open=False 로 떨어져 자동으로 더미 이미지 모드로 폴백 →
  개발/CI 환경에서도 파이프라인 동작. `config.json` 의 `ai_server.station*.camera_enabled`
  / `camera_serial` / `camera_fps` 로 스테이션별 카메라 지정 가능.
* **GuiRouter 진입 로그 추가**: 클라이언트 버튼 액션(재학습/이력/통계/모델목록/이미지)
  진입 시 fd + 파라미터를 즉시 로그 → 추적성 확보.
* **재학습 플래그 누수 수정**: `GuiService::request_retrain` 의 소켓 생성/송신 실패
  경로에서 `is_training_` 해제 누락 2건 수정 (영구 "학습중" 상태 방지).
* **Station2 PatchCore 학습 데이터 경로 버그 수정**: `_train_patchcore` 가
  Station2 의 경우 `./data/station2/patchcore/` 를 사용하도록 조건 분기 추가
  (기존엔 `./data/station{N}/normal` 고정으로 Station2 PatchCore 학습이 실제로 불가).

### v0.12.0 — NG 파이프라인 비동기 분리 (ACK 지연 근본 해결)

* **문제**: v0.11.x 까지 `StationHandler` 가 `InspectionService::process()` 를
  동기 호출 → 이미지 3장 저장(수 MB 디스크 I/O) + DB INSERT 가 끝나야 ACK 발행.
  실측 500ms~1s+ 소요 → AI 서버 `ACK_TIMEOUT_SEC=1.0` 이 반복 타임아웃 →
  재전송 루프 → 연결 끊김 악순환.

* **해결 구조**:
  ```
  StationHandler
    ├─ validate_only(ev)          (<1ms)
    ├─ publish ACK_SEND_REQUESTED  ← 즉시 ACK
    └─ publish INSPECTION_VALIDATED ← 백그라운드 위임
                                     ↓
                 InspectionService::on_validated  (EventBus 워커 스레드)
                   ├─ save_blob × 3 (원본/히트맵/마스크)
                   ├─ INSERT inspections
                   ├─ INSERT assemblies (Station2)
                   └─ publish GUI_PUSH_REQUESTED
  ```

* **신규 이벤트**: `EventType::INSPECTION_VALIDATED` — 검증 통과 후 영속화
  위임용. EventBus 의 N개 워커가 병렬 처리.

* **Sliced failure 정책**: ACK 가 이미 송신된 뒤 persist 가 실패하면 재전송 불가.
  `[ERR] [DB] [SLICED-FAILURE] 저장 실패 (ACK 는 이미 송신됨) | id=... err=...`
  ERROR 레벨로 강하게 남겨 운영자 추적 가능.

* **효과**:
  - AI 서버 체감 ACK 지연: 500ms+ → **수 ms** (validate_only 만)
  - `ACK_TIMEOUT_SEC`: 1초 → **3초** (여유)
  - 고부하(초당 수십건 NG) 에서도 ACK 타임아웃 사라짐

### v0.12.0 기타 안정화

* **검증기 정합성 수정** (`InspectionService::validate`):
  - `result` 필드: `"OK"/"NG"` 대문자 수용 + 소문자 정규화 (AI서버 송신 형식)
  - `score` 필드: 0~1 강제 제거 (PatchCore anomaly_score 는 무한대 범위) →
    `std::isfinite()` 로 NaN/Inf 만 차단
  - 기존엔 이 두 버그로 모든 NG 가 `invalid_result` / `invalid_score` 로 거부됨

* **INSPECT_NG_ACK_EXT(111) 처리**: 클라이언트가 NG 수신 후 자동 ACK(111) 를
  보내는데 서버가 "미처리 프로토콜" 로 로깅하던 노이즈 제거. EXT_ACK(190) 와
  동일하게 silent pass.

### v0.13.0 — 클라이언트 학습 이미지 업로드 End-to-End

**배경**: v0.12.x 까지 클라 "폴더 선택" 은 파일명 수집만 했고, 실제 학습은
학습서버에 미리 준비된 `./data/station*/...` 폴더로만 이뤄짐 → UI 와 실제
동작 불일치. 제품 전환 시 학습 데이터 교체가 번거로웠음.

**해결**: 3 컴포넌트(클라/메인/학습) 동시 확장으로 엔드투엔드 업로드 파이프라인
구현. 신규 프로토콜 4 개 추가:

| 번호 | 이름 | 방향 | 역할 |
|------|------|------|------|
| 158 | `RETRAIN_UPLOAD` | 클라→메인 | 학습용 이미지 1장 업로드 (JSON + binary) |
| 159 | `RETRAIN_UPLOAD_ACK` | 메인→클라 | 파일별 업로드 ACK + 진행률 |
| 1108 | `TRAIN_DATA_UPLOAD` | 메인→학습 | 이미지 중계 (JSON + binary) |
| 1109 | `TRAIN_DATA_UPLOAD_ACK` | 학습→메인 | 저장 결과 ACK |

**동작 흐름**:
```
① 클라: 폴더 선택 → 파일 개수 + 폴더 경로 보관
② 클라: "재학습 실행" 클릭 → session_id 생성 (sess-YYYYMMDD-HHMMSS-NNNNN)
   → 파일별로 RETRAIN_UPLOAD(158) 순차 송신
③ 메인: 파일 수신 → ./storage/training_upload/{session_id}/{file} 로컬 저장
   → 학습서버 TCP 로 TRAIN_DATA_UPLOAD(1108) 중계
④ 학습서버: ./data/station{N}/uploads/{session_id}/{file} 저장 → ACK(1109)
⑤ 메인: ACK(159) 를 클라에 회신 → 진행률 표시 (0~50%)
⑥ 클라: 모든 ACK 수신 완료 → RETRAIN_REQ(152) 에 session_id 동봉하여 송신
⑦ 메인: TRAIN_START_REQ(1100) 에 data_path="./data/station{N}/uploads/{session_id}" 주입
⑧ 학습서버: data_path 로 학습 실행 (기본 경로 대신 업로드 폴더 사용)
```

**주요 안전장치**:
- 파일명 path traversal 차단 (basename 화 + ".." / "/" / "\\" 검사)
- 파일당 50MB 상한
- `image_size` > 0 이면 GUI 리스너가 JSON 뒤 바이너리를 추가 수신 (`recv_one_request` 확장)
- 메인 로컬 저장 실패해도 학습서버 중계는 계속 시도 → 이중 저장으로 복구력 강화

**신규 클래스/함수**:
- C++  `GuiService::receive_retrain_upload()` — 수신 + 중계 + ACK 파싱
- C++  `GuiRouter::handle_retrain_upload()` — 요청 분기
- C++  `GuiTcpListener::recv_one_request(json, binary)` — 바이너리 동반 수신
- Py   `TrainingServer._handle_train_data_upload()` — 디스크 저장 + ACK
- MFC  `CPacketBuilder::BuildRetrainUploadFrame()` / `GenerateSessionId()`
- MFC  `CPageModel::OnRetrainUploadAck()` — ACK 수신 → 진행률 + RETRAIN_REQ 자동 발행

### v0.14.0 — 검사 pause/resume 원격 제어

클라이언트 메뉴 [검사 > 시작/중지] 가 **실제 AI 추론서버의 grab 루프를 중단/재개** 하도록 연결됨.
이전엔 로컬 시뮬레이션 타이머만 on/off 하는 더미 동작이었음.

**신규 프로토콜**:
| 번호 | 이름 | 방향 |
|---|---|---|
| 160 | `INSPECT_CONTROL_REQ` | 클라 → 메인 |
| 161 | `INSPECT_CONTROL_RES` | 메인 → 클라 |
| 1020 | `INFERENCE_CONTROL_CMD` | 메인 → 추론 |
| 1021 | `INFERENCE_CONTROL_RES` | 추론 → 메인 |

**흐름**:
```
MFC 메뉴 [검사 > 중지]
  ↓ INSPECT_CONTROL_REQ(160) {action:"pause", station_filter:0}
MainServer: ConnectionRegistry 에서 ai_inference_* 찾아 각각에 중계
  ↓ INFERENCE_CONTROL_CMD(1020) {action:"pause"}
추론서버 TcpClient → StationRunner._handle_inference_control
  ↓ self._pause_event.clear()
_run_grab_producer: pause_event.wait() 에서 블록 → 카메라 grab 중단
  → 추론/송신 파이프라인 자연스럽게 멈춤
  ↓ INFERENCE_CONTROL_RES(1021) ACK
MainServer → 클라 INSPECT_CONTROL_RES(161) → MessageBox 알림
```

**구현 특징**:
- `asyncio.Event` 기반 → resume 시 grab 루프가 **즉시** 깨어남 (poll 없음)
- station_filter: 0=전체, 1/2=특정 스테이션만
- HealthChecker 의 server_type 태깅을 그대로 활용 (별도 식별 로직 X)

### v0.14.5~v0.14.7 — 안정성/UI 최적화

* **v0.14.5**: JSON 파서 상한 64KB → **1MB** (200건 이력 응답 88KB 끊김 해결),
  `WSAETIMEDOUT` 재시도 정책(2분 하드리밋), IDT_RECONNECT 10초 → 2초
* **v0.14.6**: NG 3장 이미지(원본/히트맵/마스크) 네트워크 전송 안정화 —
  긴 변 1280 이하로 다운스케일, 더미 레코드 생성 완전 제거
* **v0.14.7**:
  - `NG_PUSH(110)` JSON 에 **`"id"` (DB AUTO_INCREMENT) 필드 추가** — MFC NG 리스트 중복방지 키
  - 학습서버 notify 양방향 통신(`_notify_recv_loop`) 추가 — HEALTH_PONG 에 `server_type="training"`
  - pause/resume 최종 단순화: 카메라 HW stop/start 제거, `_pause_event` 토글만
  - 세션 해제 3초 debounce — micro-disconnect 로그 노이즈 제거
  - `CameraView` race 방지 (`CRITICAL_SECTION` + 더블버퍼링)
  - 서버 LED tri-state (`Unknown`/`Up`/`Down`)
  - YOLO 학습 데이터 레이아웃 5종 자동 감지 (Roboflow 포함)

### v0.14.9 — Arduino 단순화

* Arduino 빨강/초록 LED 로직 단순화 (`arduino_led_control.ino` 통합)
* MFC PageStation1 임계값 "0.50" 하드코딩 제거 → "AUTO (F1 최적화)" 표기

### v0.15.0 — 통신 경로 정합성

* **MODEL_LIST_RES(151) → PageStation1 연결**: v0.14.9 에서 하드코딩 제거 후 "로딩 중..." 만
  표시되던 것을 실제 서버값으로 자동 갱신 (`MainTabDlg::OnNetResponse` 에서 주입)
* **`requires_ack()` 보강**: `RETRAIN_UPLOAD(158)` / `TRAIN_DATA_UPLOAD(1108)` 도 ACK 필수
  분류 (C++ `Protocol.h` + Python `Protocol.py` 양쪽)
* **`HEALTH_PONG(1201)` 에 `server_type` 필드 명시**: 이전엔 `station_id` 로 유추하던 것을
  `"station1"/"station2"` 로 직접 전송 → ConnectionRegistry 매칭 단순화
* **`PacketBuilder::ExtractString` 백슬래시 카운팅 수정**: `\\"` 같은 escape 패턴에서
  조기 종료 가능성 차단
* **재학습 중복 클릭 차단**: `PageModel::OnBtnRetrain` 이 진행 중일 때 MessageBox 로 명시
* **`TcpClient.py` 모델 파일 임시파일 정리 `except: pass` 제거**: WARN 로그로 추적
* **`Inferencer.active_model_id` 도입**: `INSPECT_META` 의 `model_id=0` 하드코딩 제거.
  `MODEL_RELOAD_CMD` 에 `model_db_id` 필드 포함 시 자동 세팅 (현재 서버는 미동봉 → 0 유지)
* **`ConnectionPool` 주석 보강**: nullptr 저장의 의도(shutdown double-close 회피)를 명확화

### v0.15.1 — Hotfix: DB 컬럼 정합성

* **`dao.cpp` SQL 컬럼명 `model_path` → `file_path`**: 기획서 v0.12 ERD 및 실제 운영 DB 가
  `file_path` 를 쓰는데 코드가 `model_path` 로 INSERT 시도 → "Unknown column" 에러로 학습 완료
  배포 파이프라인 전부 실패하던 문제 수정.
* **`schema.sql` 을 기획서 ERD 에 맞춰 재정의**: `ENUM('PatchCore','YOLO11')`, `VARCHAR(20)`,
  `file_path`, `trained_by FK → users.id`.
* **`ModelInfo` 구조체에 `file_path` 필드 추가** (외부 사용처 영향 없는 확장).

### v0.15.2 — PageStats (통계/이력) 탭 완전 제거 (현재)

* **5개 탭 → 4개 탭**: 종합 현황 / ① 입고 검사 / ② 조립 검사 / 모델 관리
* admin 의 `5c821f3 통계 제거 필요` 후속 작업 — `CPageStats` 클래스와 리소스 참조 완전 삭제
  (`PageStats.cpp/h` 삭제 + `MainTabDlg` 에서 `m_stats` 참조 7곳 제거 + vcxproj 엔트리 제거)
* **기능 이관**:
  - Summary 누적값 초기화: `PageHome::ApplyStatsRes` (변경 없음)
  - DB 검사 이력 리스트: `PageHome::OnInspectHistoryRes` + `PageStation1::PopulateNgHistoryFromJson`
  - NG_IMAGE 의 timestamp/score: 패킷(`pkt->timestamp_iso`, `pkt->score`) 에서 직접 추출
    (v0.14.7 에서 이미 패킷에 담김)
* **유지**: `STATS_REQ(130)` / `STATS_RES(131)`, `INSPECT_HISTORY_REQ(114)` / `_RES(115)`
  프로토콜 자체는 그대로. **MainServer / AiServer 재빌드 불필요**.

### 상세 현황
보안 수정 현황은 프로젝트 문서 참고
(CRITICAL 7 + HIGH 12 + MEDIUM 16 + LOW 11 = 총 46/47 완료)

---

## 운영 가이드

### 통합 설정 파일 (config/config.json)

프로젝트의 모든 IP/포트/경로/DB 접속정보/AI 하이퍼파라미터가
`config/config.json` 단일 파일로 통합됨 (MainServer + AiServer + MFC Client 공유).

**주요 섹션:**
* `network` — 서버 IP, AI/GUI/Training 포트, IP 화이트리스트
* `database` — MariaDB 접속 정보, 풀 크기
* `storage` — 이미지/모델/로그 저장 경로
* `limits` — 네트워크/큐/세션 상한
* `health_check` — 헬스체크 대상 목록
* `ai_server.station1` / `ai_server.station2` — 추론서버 스테이션별 설정
* `training` — 학습서버 하이퍼파라미터
* `client` — MFC 클라이언트 설정

**로드 경로 (우선순위):**

| 컴포넌트 | 경로 지정 방식 |
|---------|--------------|
| MainServer | 인자 `./factory_main_server <path>` > `CONFIG_PATH` 환경변수 > `../../config/config.json` |
| AiServer (Python) | `CONFIG_PATH` 환경변수 > `../config/config.json` 자동 탐색 |
| MFC Client | 실행파일 기준 `..\..\config\config.json` 자동 탐색 |

### 환경 변수 (선택)

* `CONFIG_PATH` — config.json 경로 지정
* `TRAIN_HOST` — 학습서버 IP (config.json보다 우선)
  ```bash
  TRAIN_HOST=192.168.0.50 ./factory_main_server
  ```

### 시각 동기화 (NTP)

AI 서버, 메인 서버, 클라이언트 간 시각이 다르면 `inspection_id`의 timestamp 부분이 불일치해 로그 추적이 어려워진다. 운영 시 모든 PC에 NTP 동기화 설정 필수.

```bash
# Ubuntu / WSL
sudo timedatectl set-ntp true

# Windows
w32tm /config /manualpeerlist:"time.windows.com" /syncfromflags:manual /update
```

### 로그 파일

* 경로: `MainServer/build/logs/YYYY-MM-DD.log`
* 자정 자동 로테이션
* stdout과 동시 출력 (tee 방식)

---

## 디렉터리 구조

```
Factory/
├── config/
│   └── config.json       # 통합 설정 (모든 IP/포트/경로/DB/하이퍼파라미터)
├── MainServer/           # 메인 운영 서버 (C++17)
│   ├── include/security/ # json_safety, input_validator, ip_filter
│   ├── include/core/     # config.h, event_bus, tcp_listener, tcp_utils, logger
│   ├── src/core/         # EventBus, TcpListener, Config
│   ├── src/handler/      # Router, StationHandler, AckSender, TrainHandler
│   ├── src/service/      # InspectionService, TrainService
│   ├── src/storage/      # ConnectionPool, DAO, PasswordHash
│   ├── src/session/      # GuiTcpListener, GuiRouter, GuiService, SessionManager
│   └── src/monitor/      # HealthChecker, ConnectionRegistry
├── AiServer/             # AI 추론/학습 서버 (Python)
│   ├── Common/           # ConfigLoader, Protocol, TcpClient, Inferencer, StationRunner
│   ├── Station1/         # 입고 검사 진입점
│   ├── Station2/         # 조립 검사 진입점
│   ├── Training/         # 학습 서버
│   └── tests/            # 테스트 스크립트
└── client/Factory_UI_CL/ # MFC 클라이언트
    ├── ClientConfig.*    # config.json 로더
    ├── NetworkClient.*   # TCP 통신
    ├── PacketBuilder.*   # JSON 빌더 (이스케이프 포함)
    └── Main/Login/Page* # UI
```

상세 구조: `Directory_README.md` 참고

---

## 통신 구조

* TCP 기반 통신
* JSON + Binary Image 패킷 구조: `[4byte BE length] + [JSON] + [이미지]`
* ACK / RETRY / NACK 지원

상세 프로토콜: `Protocol_README.md` 참고

---

## DB

* MariaDB 사용
* 5개 테이블: users, models, bottles, inspections, assemblies

접속 정보: `DB_README.md` 참고

---

## 실행 방법

### Main Server

```
cd MainServer
mkdir build && cd build
cmake ..
cmake --build .
./factory_main_server
```

### AI Server

```
cd AiServer
python -m Station1.Station1Main      # 입고 검사
python -m Station2.Station2Main      # 조립 검사
```

---

## 현재 상태

### 완성

* AI 서버 전체 파이프라인 (카메라 grab → 추론 → TCP 송신 → ACK 수신)
* 메인 서버 NG 검사 흐름 (수신 → 파싱 → DB INSERT → 이미지 저장 → ACK 회신)
* 프로토콜 동기화 (C++ ↔ Python 완벽 일치)
* MariaDB 연동 (prepared statement)
* SessionManager + GuiNotifier (MFC broadcast)
* 학습 서버 (PatchCore / YOLO11)
* 학습서버 ↔ 메인서버 연동 (TRAIN_PROGRESS/COMPLETE/FAIL 수신 → DB models INSERT → GUI 푸시)
* 메인서버 → 학습서버 TRAIN_START_REQ 전송 (클라이언트 재학습 버튼 연동)
* MFC 클라이언트 전체 프로토콜 구현 (100~199)
  * 로그인/회원가입 — DB 기반 인증 (하드코딩 제거)
  * 검사 이력 조회, 통계 조회, 모델 목록 조회
  * 재학습 요청/진행률 실시간 수신
* 한글 행동 중심 로그 시스템 (logger.h — 이모지 prefix, 역할별 분류)
* 전체 메인서버 코드 한글 주석
* 테스트 스크립트 + 더미 데이터 생성기

### 미완성

* NG 이미지 CameraView 렌더링 (클라이언트)
* 카메라 실시간 연동 (하드웨어)

---

## 향후 계획

### 하드웨어 연동
* ✅ Pylon 카메라 SDK 연동 (v0.11.0 — 미연결/미설치 시 자동 더미 폴백)
* Arduino 시리얼 통신 연동
* ✅ HealthChecker 동적 서버 감지 (v0.11.0 — server_type 기반 매칭)

### 확장 기능
* 모델 정확도 개선 및 최적화
* NG 이미지 CameraView 실시간 렌더링 (클라이언트)

### 실무 수준 확장 (참고)
* ✅ `config.json` 단일 설정 파일로 IP/포트/경로 통합 (2026-04-18)
* ✅ `security/` 모듈로 보안 로직 집약 (json_safety, input_validator, ip_filter)
* ✅ 파일 로거 (logs/YYYY-MM-DD.log 자동 로테이션) (2026-04-18)
* ✅ UI 연결 상태 실시간 동기화 (2026-04-20)
* ☐ `logrotate` 설정 (장기 운영 디스크 관리)
* ☐ Google Test + Mock DAO 기반 Service 레이어 단위 테스트
* ☐ `IConnection` 인터페이스로 TCP/TLS 교체 가능 구조
* ☐ 청크 단위 모델 전송 + Resume (GB급 PatchCore 모델 대응)

---

## 📚 참고 문서

* [Protocol_README.md](Protocol_README.md) — 프로토콜 상세 스펙
* [DB_README.md](DB_README.md) — DB 스키마, 쿼리 예시
* [AiServer_README.md](AiServer_README.md) — AI 서버 파이프라인
* [Client_README.md](Client_README.md) — MFC 클라이언트 구조
* [Directory_README.md](Directory_README.md) — 디렉터리 상세
* [ARCHITECTURE_PRINCIPLES.txt](ARCHITECTURE_PRINCIPLES.txt) — SOLID/패턴 정리
