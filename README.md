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
                   (동적 주기 감시)      (GUI 세션 관리)
```

### 구성 요소

* **AiServer (Python 3.10+)**

  * asyncio.Queue 기반 비동기 파이프라인
  * Station1: PatchCore 이상탐지 (입고 검사)
  * Station2: YOLO11 단독 (조립 검사)
  * NG 데이터만 서버 전송 (네트워크 최적화)
  * ACK 기반 재전송 (3초 타임아웃, 최대 3회)
  * Pylon 카메라 SDK 연동 + 더미 모드 자동 폴백

* **MainServer (C++17)**

  * EventBus (워커 풀 4스레드 병렬 처리) + Service 레이어 아키텍처
  * Handler → Service(검증+트랜잭션) → DAO(DB) 계층 분리
  * ConnectionPool (4개 DB 커넥션 공유)
  * bcrypt 비밀번호 해시 (glibc crypt_r)
  * 추론서버 수신 (포트 9000) + GUI 클라이언트 수신 (포트 9010)
  * 모델 바이너리 TCP 중계 (학습서버 → 메인서버 → 추론서버)
  * SessionManager를 통한 MFC 클라이언트 broadcast
  * HealthChecker 동적 서버 감지 (server_type 기반 매칭)

* **Training Server (Python)**

  * TCP 서버 (포트 9100)
  * PatchCore / YOLO11 학습
  * 학습 진행률 송신 + 완료/실패 알림
  * 클라이언트 업로드 이미지 기반 학습 데이터 교체 지원

* **MFC Client**

  * 4탭 UI — 종합 현황 / 입고 검사 / 조립 검사 / 모델 관리
  * 실시간 NG 검사 결과 모니터링 + CameraView 렌더링
  * 검사 이력 / 통계 조회
  * 재학습 요청 + 학습 데이터 업로드 + 진행률 실시간 수신
  * 사용자 인증 — DB 기반 RBAC (admin/operator/viewer)

* **Database (MariaDB)**

  * inspections / assemblies / bottles / models / users 테이블
  * inspection_id 기반 end-to-end 추적

---

## 전체 데이터 흐름

1. 카메라에서 이미지 캡처
2. AI 서버에서 OK / NG 판정
3. 모든 검사 결과 → `INSPECT_META(1006)` 전송 (fire-and-forget)
4. NG 발생 시 → `STATION_NG(1000/1002)` 전송 (ACK 필수)
5. Main Server에서 검증 즉시 ACK 회신
6. EventBus 워커가 백그라운드로 DB INSERT + NG 이미지 저장
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
* **HealthChecker 동적 주기**: 모든 서버 정상 시 30초, 장애 발생 시 5초 주기로 전환

### 데이터 흐름
* NG 중심 전송 구조 (트래픽 최적화)
* inspection_id 기반 end-to-end 추적
* ACK / 재전송 기반 데이터 신뢰성 확보
* **검증 즉시 ACK → 백그라운드 영속화** (INSPECTION_VALIDATED 이벤트)
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

## 주요 기능

### NG 파이프라인 비동기 분리

검증과 영속화를 분리하여 ACK 지연을 근본적으로 해결한 구조이다.

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

* AI 서버 체감 ACK 지연: 500ms+ → **수 ms** (validate_only 만)
* 고부하(초당 수십건 NG) 에서도 ACK 타임아웃 방지
* Sliced failure(ACK 송신 후 persist 실패) 케이스는 ERROR 레벨 로그로 운영자 추적

### 학습·배포 파이프라인 (멀티호스트)

학습서버를 메인서버와 별도 PC에 배치할 수 있는 분리 구조.

* `config.json` 의 `network.training_server_host` 키 — 학습서버 IP 분리 지정
* 환경변수 `TRAIN_HOST` 로 오버라이드 가능
* `MODEL_RELOAD_CMD` 의 `model_type` 필드 — Station2 이중모델(YOLO + PatchCore) 슬롯 명시
* 추론서버 수신측 필터 — `station_id` / `model_type` 기반 슬롯 라우팅
* HealthChecker 의 `server_type` 기반 매칭 — IP 하드코딩 제거,
  배포 PC 가 바뀌어도 설정 수정 불필요

### 클라이언트 학습 이미지 업로드

클라이언트에서 직접 학습용 이미지를 업로드하여 학습 데이터를 교체할 수 있는 엔드투엔드 파이프라인.

| 번호 | 이름 | 방향 | 역할 |
|------|------|------|------|
| 158 | `RETRAIN_UPLOAD` | 클라→메인 | 학습용 이미지 1장 업로드 (JSON + binary) |
| 159 | `RETRAIN_UPLOAD_ACK` | 메인→클라 | 파일별 업로드 ACK + 진행률 |
| 1108 | `TRAIN_DATA_UPLOAD` | 메인→학습 | 이미지 중계 (JSON + binary) |
| 1109 | `TRAIN_DATA_UPLOAD_ACK` | 학습→메인 | 저장 결과 ACK |

**동작 흐름**:
```
① 클라: 폴더 선택 → session_id 생성 → 파일별 순차 송신
② 메인: ./storage/training_upload/{session_id}/{file} 로컬 저장
   → 학습서버로 TRAIN_DATA_UPLOAD 중계
③ 학습서버: ./data/station{N}/uploads/{session_id}/{file} 저장 → ACK
④ 메인: ACK 를 클라에 회신 → 진행률 표시 (0~50%)
⑤ 클라: 모든 ACK 수신 → RETRAIN_REQ 에 session_id 동봉하여 송신
⑥ 학습: 업로드 폴더 기반으로 학습 실행
```

**안전장치**:
- 파일명 path traversal 차단 (basename 화 + `..` / `/` / `\\` 검증)
- 파일당 50MB 상한
- 메인 로컬 저장 실패해도 학습서버 중계는 계속 시도 (이중 저장 복구력)

### 검사 pause/resume 원격 제어

클라이언트 메뉴에서 **실제 AI 추론서버의 grab 루프를 중단/재개** 하도록 연결.

| 번호 | 이름 | 방향 |
|---|---|---|
| 160 | `INSPECT_CONTROL_REQ` | 클라 → 메인 |
| 161 | `INSPECT_CONTROL_RES` | 메인 → 클라 |
| 1020 | `INFERENCE_CONTROL_CMD` | 메인 → 추론 |
| 1021 | `INFERENCE_CONTROL_RES` | 추론 → 메인 |

* `asyncio.Event` 기반 — resume 시 grab 루프가 **즉시** 깨어남 (poll 없음)
* `station_filter`: 0=전체, 1/2=특정 스테이션만
* `server_type` 태깅을 그대로 활용하여 별도 식별 로직 불필요

---

## 보안 강화 내역

### 적용된 방어

* **SQL Injection 차단**: 모든 DAO prepared statement 사용
* **IP 화이트리스트**: AI 서버 포트 9000에 내부망(10.x, 192.168.x, 172.16~31.x)만 허용
* **입력 크기 제한**: JSON 1MB, 이미지 50MB, 모델 500MB 상한
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
* Pylon 카메라 SDK 연동 (미연결/미설치 시 자동 더미 폴백)
* HealthChecker 동적 서버 감지 (server_type 기반 매칭)
* NG 이미지 CameraView 렌더링 (클라이언트)
* Arduino 시리얼 통신 연동

---

## 향후 계획

### 하드웨어 연동
* Arduino 시리얼 통신 연동
* NG 이미지 CameraView 실시간 렌더링 (클라이언트)

### 확장 기능
* 모델 정확도 개선 및 최적화

### 실무 수준 확장예상
* `logrotate` 설정 (장기 운영 디스크 관리)
* Google Test + Mock DAO 기반 Service 레이어 단위 테스트
* `IConnection` 인터페이스로 TCP/TLS 교체 가능 구조
* 청크 단위 모델 전송 + Resume (GB급 PatchCore 모델 대응)

---

## 📚 참고 문서

* [Protocol_README.md](Protocol_README.md) — 프로토콜 상세 스펙
* [DB_README.md](DB_README.md) — DB 스키마, 쿼리 예시
* [AiServer_README.md](AiServer_README.md) — AI 서버 파이프라인
* [Client_README.md](Client_README.md) — MFC 클라이언트 구조
* [Directory_README.md](Directory_README.md) — 디렉터리 상세
* [ARCHITECTURE_PRINCIPLES.txt](ARCHITECTURE_PRINCIPLES.txt) — SOLID/패턴 정리
