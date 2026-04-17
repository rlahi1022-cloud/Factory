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

* EventBus + Service 레이어 서버 아키텍처 (책임 분리)
* Handler → Service → DAO 3계층 구조 (검증 + 트랜잭션 + 롤백)
* ConnectionPool 기반 DB 커넥션 관리
* asyncio.Queue 기반 비동기 처리 (Backpressure 대응)
* NG 중심 전송 구조 (트래픽 최적화)
* inspection_id 기반 end-to-end 추적
* ACK / 재전송 기반 데이터 신뢰성 확보
* MariaDB prepared statement (SQL injection 방지)
* 모델 바이너리 TCP 전송 (학습서버 → 메인서버 → 추론서버)
* SessionManager 기반 다중 GUI 클라이언트 broadcast

---

## 디렉터리 구조

```
Factory/
├── MainServer/       # 메인 운영 서버 (C++17)
│   ├── src/core/     # EventBus, TcpListener, main
│   ├── src/handler/  # Router, StationHandler, AckSender
│   ├── src/storage/  # DbManager, ImageStorage
│   ├── src/session/  # SessionManager, GuiTcpListener, GuiNotifier
│   └── src/monitor/  # HealthChecker, ConnectionRegistry
└── AiServer/         # AI 추론/학습 서버 (Python)
    ├── Common/       # 공통 모듈 (Protocol, Packet, TcpClient, Inferencer, StationRunner)
    ├── Station1/     # 입고 검사 진입점
    ├── Station2/     # 조립 검사 진입점
    ├── Training/     # 학습 서버
    └── tests/        # 테스트 스크립트
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

* CSV 내보내기 기능 (클라이언트)
* NG 이미지 UI 표시 (클라이언트)

---

## 향후 계획

* Pylon 카메라 SDK 연동 (현재 더미 이미지)
* Arduino 실제 시리얼 통신 연동

---

## 향후 계획

* HealthChecker PING/PONG 프로토콜 완성
* Pylon 카메라 SDK 연동 (현재 더미 이미지)
* 비밀번호 bcrypt 해시 적용
* 모델 정확도 개선 및 최적화
