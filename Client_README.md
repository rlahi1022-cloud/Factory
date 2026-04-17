# MFC Client — Factory QC 모니터링 클라이언트

## 개요

MFC(Microsoft Foundation Class) 기반 Windows 데스크탑 애플리케이션.
메인서버(포트 9010)에 TCP 접속하여 실시간 검사 결과를 수신하고,
검사 이력/통계 조회 및 모델 관리 기능을 제공한다.

## 접속 정보

| 항목 | 값 |
|------|-----|
| 서버 IP | `10.10.10.130` |
| 포트 | `9010` (GuiTcpListener) |
| 패킷 포맷 | `[4byte BE length] + [JSON UTF-8]` |
| 프로토콜 버전 | `1.0` |

## 계정 (서버 DB 인증)

로그인 시 서버에 `LOGIN_REQ(100)`을 전송하여 MariaDB `users` 테이블에서 인증한다.
회원가입은 `REGISTER_REQ(104)`로 서버에 전송하여 DB에 INSERT한다.

초기 계정은 DB에 직접 추가:
```sql
INSERT INTO users (employee_id, username, password_hash, role)
VALUES ('EMP-001', 'admin01', '1234', 'Admin');
```

## 화면 구성 (5개 탭)

| 탭 | 클래스 | 기능 |
|----|--------|------|
| 종합 현황 | CPageHome | OK/NG 누적, 불량률, 스테이션별 현황, NG 이력 리스트 |
| 입고 검사 | CPageStation1 | PatchCore 결과 (카메라뷰 + 히트맵 + 이상점수) |
| 조립 검사 | CPageStation2 | YOLO11 결과 (카메라뷰 + 디텍션 + 히트맵) |
| 통계/이력 | CPageStats | 시간대별 추세, 파레토 차트, 레이턴시 분포 |
| 모델 관리 | CPageModel | 배포 모델 목록, 재학습 요청/진행률 |

## 디렉터리 구조

```
client/
├── Factory_UI_CL.slnx                # Visual Studio 솔루션
├── Factory_UI_CL/
│   ├── ClientProtocol.h              # 프로토콜 번호 정의 (100~199)
│   ├── PacketBuilder.h/cpp           # 패킷 조립/파싱, 각 요청별 JSON 빌더
│   ├── NetworkClient.h/cpp           # TCP 클라이언트 (비동기 수신 스레드, 자동 재접속)
│   ├── InspectionData.h/cpp          # 데이터 구조체 (InspectionRecord, UserSession)
│   ├── LoginDlg.h/cpp                # 로그인/회원가입 다이얼로그
│   ├── MainTabDlg.h/cpp              # 메인 윈도우 (5개 탭, 네트워크 핸들러, 타이머)
│   ├── PageHome.h/cpp                # 종합 현황 페이지
│   ├── PageStation1.h/cpp            # 입고 검사 페이지
│   ├── PageStation2.h/cpp            # 조립 검사 페이지
│   ├── PageStats.h/cpp               # 통계/이력 페이지
│   ├── PageModel.h/cpp               # 모델 관리 페이지
│   ├── CameraView.h/cpp              # 카메라/히트맵 커스텀 뷰
│   ├── Factory_UI_CL.h/cpp           # MFC 앱 클래스
│   ├── Factory_UI_CLDlg.h/cpp        # 앱 진입점 다이얼로그
│   ├── Resource.h                    # 리소스 ID 정의
│   ├── FactoryUICL.rc                # 리소스 파일 (다이얼로그, 아이콘)
│   └── res/                          # 아이콘 등 리소스
└── tests/
    ├── TestPacketBuilder.cpp          # 패킷 빌더 단위 테스트 (콘솔)
    └── TestPacketBuilder.exe          # 빌드된 테스트 실행파일
```

## 통신 흐름

### 로그인

```
클라이언트                          메인서버 (9010)
    │                                   │
    │── LOGIN_REQ(100) ────────────────>│
    │   {username, password,            │
    │    request_id, timestamp}         │
    │                                   │
    │<──────────────── LOGIN_RES(101) ──│
    │   {success, role, employee_id,    │
    │    message, timestamp}            │
```

### 실시간 NG 수신 (서버 → 클라이언트 push)

```
클라이언트                          메인서버
    │                                   │
    │<────── INSPECT_NG_PUSH(110) ──────│
    │   {inspection_id, station_id,     │
    │    result, defect_type, score,    │
    │    latency_ms, timestamp}         │
    │                                   │
    │── INSPECT_NG_ACK_EXT(111) ───────>│
    │   {inspection_id}                 │
```

### 서버 상태 수신

```
클라이언트                          메인서버
    │                                   │
    │<──── SERVER_HEALTH_PUSH(170) ─────│
    │   {server_name, ip, port, status} │
    │                                   │
    │  → 툴바 LED 업데이트              │
```

## 프로토콜 메시지 (클라이언트 ↔ 메인서버)

### 클라이언트 → 서버 (요청)

| protocol_no | 이름 | JSON 필드 | 서버 구현 |
|-------------|------|-----------|----------|
| 100 | LOGIN_REQ | username, password, request_id, timestamp | ✅ 완성 (DB 인증) |
| 102 | LOGOUT_REQ | username, timestamp | ✅ 완성 |
| 104 | REGISTER_REQ | username, password, employee_id, role, request_id | ✅ 완성 (DB INSERT) |
| 114 | INSPECT_HISTORY_REQ | station_filter, date_from, date_to, limit, request_id | ✅ 완성 |
| 130 | STATS_REQ | station_filter, date_from, date_to, request_id | ✅ 완성 |
| 150 | MODEL_LIST_REQ | request_id, timestamp | ✅ 완성 |
| 152 | RETRAIN_REQ | station_id, model_type, product_name, image_count, request_id | ✅ 완성 |

### 서버 → 클라이언트 (응답/push)

| protocol_no | 이름 | 수신 핸들러 | 서버 구현 |
|-------------|------|-----------|----------|
| 101 | LOGIN_RES | OnLoginRes() | ✅ 완성 |
| 103 | LOGOUT_RES | OnNetResponse() | ✅ 완성 |
| 105 | REGISTER_RES | OnRegisterRes() | ✅ 완성 |
| 110 | INSPECT_NG_PUSH | OnNetNgPush() | ✅ 완성 (JSON + 이미지 바이너리) |
| 112 | INSPECT_OK_COUNT_PUSH | OnNetOkCountPush() | ✅ 완성 |
| 115 | INSPECT_HISTORY_RES | OnNetResponse() | ✅ 완성 |
| 131 | STATS_RES | OnNetResponse() | ✅ 완성 |
| 151 | MODEL_LIST_RES | OnNetResponse() | ✅ 완성 |
| 153 | RETRAIN_RES | OnNetResponse() | ✅ 완성 |
| 154 | RETRAIN_PROGRESS_PUSH | OnNetRetrainProgress() | ✅ 완성 |
| 170 | SERVER_HEALTH_PUSH | OnNetHealthPush() | ✅ 완성 |

## NetworkClient 설계

```
┌──────────────────┐   PostMessage    ┌──────────────────┐
│  수신 스레드       │  ──────────────> │  UI 스레드        │
│  (RecvLoop)       │  WM_NET_xxx     │  (MainTabDlg)    │
└──────────────────┘                   └──────────────────┘
     ↑ recv()                              ↓ SendJson()
┌──────────────────────────────────────────────────────────┐
│              메인 서버 (포트 9010)                         │
└──────────────────────────────────────────────────────────┘
```

- 수신 타임아웃 5초: 타임아웃 시 EXT_ACK(190) heartbeat 전송
- 서버 미연결 시 10초마다 재접속 시도 (IDT_RECONNECT 타이머)
- ACK 필요 메시지(110, 156) 수신 시 자동 ACK 응답

## 빌드

Visual Studio 2022에서 `Factory_UI_CL.slnx` 열기 → 빌드 (Debug/Release).

### 테스트 실행

```
cd client/tests
TestPacketBuilder.exe
```

## 구현 상태

### 완성

- TCP 접속/수신/재접속 (NetworkClient)
- 패킷 조립/파싱 (PacketBuilder)
- 서버 DB 기반 로그인/회원가입 (LOGIN_REQ/REGISTER_REQ)
- 5개 탭 UI 레이아웃 및 렌더링
- NG 결과 실시간 수신 + 표시
- OK/NG 카운트 실시간 수신
- 검사 이력 조회 (INSPECT_HISTORY_REQ)
- 통계 조회 (STATS_REQ)
- 모델 목록 조회 (MODEL_LIST_REQ)
- 재학습 요청/진행률 (RETRAIN_REQ/RETRAIN_PROGRESS_PUSH)
- 서버 헬스 LED 표시
- 시뮬레이션 모드 (서버 미연결 시 더미 데이터)

### 미완성

- NG 이미지 UI 표시 (바이너리 수신은 구현, CameraView 렌더링 미구현)
- 카메라 실시간 연동 (현재 시뮬레이션 뷰)
