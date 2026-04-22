# 디렉터리 구조

```
Factory/
├── config/           # 통합 설정 (v0.7.0+)
│   └── config.json   # 모든 IP/포트/경로/DB/하이퍼파라미터 단일 소스
├── MainServer/       # 메인 운영 서버 (C++17, 이벤트 버스 아키텍처)
├── AiServer/         # AI 추론 서버 (Python 3.10+, asyncio.Queue 비동기 큐)
├── client/           # MFC Windows 클라이언트 (Visual Studio)
├── README.md
├── Protocol_README.md
├── DB_README.md
├── Client_README.md
├── AiServer_README.md
├── Directory_README.md
└── ARCHITECTURE_PRINCIPLES.txt  # SOLID/패턴 정리 (이력서/면접용)
```

학습 서버는 `AiServer/Training/` 디렉터리에 포함되어 있으며,
학습 완료된 가중치(`*.ckpt`, `*.pt`)는 TCP 바이너리 전송으로
메인서버(130)를 경유하여 추론서버에 배포됩니다 (MODEL_RELOAD_CMD).

**배포 라우팅 (v0.11.0):** MODEL_RELOAD_CMD JSON 에 `station_id` + `model_type`
(PatchCore/YOLO11) 필드가 포함되어, 추론서버가 자신의 스테이션이 아닌 메시지는 무시하고
Station2 이중모델(YOLO + PatchCore) 중 해당 슬롯만 교체합니다. 메인서버는
학습서버 주소를 `network.training_server_host` (기본값 10.10.10.120) 에서 읽어
별도 PC에 학습서버를 배치할 수 있습니다.

**HealthChecker 동적 감지 (v0.11.0):** `config.json` 의 `health_check.targets.ip`
를 비워두면 `ConnectionRegistry` 에 태깅된 `server_type` 으로 매칭 →
추론/학습 서버를 임의의 PC 에 배치해도 자동 감지. 로그에는 실제 접속 IP 표시.

**Pylon 카메라 연동 (v0.11.0):** `AiServer/Common/PylonCamera.py` 가 Basler
Pylon SDK 를 감싸 실제 프레임 grab. pypylon 미설치/카메라 미연결 환경에서는
자동으로 더미 이미지 모드로 폴백.

## 명명 규칙

| 대상     | 규칙       | 예시 |
|----------|------------|------|
| 파일명   | snake_case | `station_handler.cpp`, `event_bus.h` |
| 클래스명 | PascalCase | `Station1Handler`, `EventBus` |
| 멤버변수 | snake_case + `_` 접미사 | `event_bus_`, `is_running_` |
| 로컬변수 | snake_case | `anomaly_score`, `result_dict` |
| 함수명   | snake_case | `register_handlers()`, `send_ack()` |
| 파라미터 | snake_case | `sender_addr`, `protocol_no` |
| Enum 이름 | PascalCase | `EventType`, `ProtocolNo` |
| Enum 값  | UPPER_SNAKE_CASE | `PACKET_RECEIVED`, `DB_WRITE_COMPLETED` |
| 구조체명 | PascalCase | `InspectionEvent`, `GuiSession` |
| 구조체 멤버 | snake_case (prefix 없음) | `json_payload`, `image_bytes` |
| 전역변수 | `g_` prefix + snake_case | `g_should_exit` |

## MainServer (C++)

```
MainServer/
├── CMakeLists.txt
├── common/
│   └── Protocol.h                      # 메시지 번호 enum(ProtocolNo), ACK 매핑, PROTOCOL_VERSION
│
├── include/
│   ├── core/                           # 핵심 인프라
│   │   ├── config.h                    # ⭐ JSON 통합 설정 로더 (v0.7.0+)
│   │   ├── event_bus.h                 # 워커 풀(4스레드) 기반 이벤트 버스
│   │   ├── event_types.h               # EventType enum + 페이로드 struct
│   │   ├── logger.h                    # 상태 중심 로그 + 파일 로거 (logs/YYYY-MM-DD.log)
│   │   ├── tcp_listener.h              # 추론서버용 TCP 리스너 (포트 9000)
│   │   └── tcp_utils.h                 # ⭐ send_all / send_json_frame (partial send 재시도)
│   │
│   ├── security/                       # ⭐ 보안 유틸 모듈 (v0.7.0+)
│   │   ├── json_safety.h               # escape_json (JSON injection 방지)
│   │   ├── input_validator.h           # is_valid_date, is_safe_inspection_id, ...
│   │   └── ip_filter.h                 # is_allowed_ip (사설망 화이트리스트)
│   │
│   ├── handler/                        # 이벤트 핸들러
│   │   ├── router.h                    # protocol_no 기반 이벤트 분기
│   │   ├── station_handler.h           # Station1/2Handler → InspectionService 호출
│   │   ├── train_handler.h             # 학습 완료 → TrainService 호출
│   │   └── ack_sender.h               # 추론서버로 ACK/NACK 회신 + MODEL_RELOAD 전송
│   │
│   ├── service/                        # 비즈니스 로직 (검증 + 트랜잭션)
│   │   ├── inspection_service.h        # 검증 → DB INSERT → 이미지 저장 (트랜잭션)
│   │   └── train_service.h             # 검증 → 모델 파일 저장 → DB INSERT (롤백 지원)
│   │
│   ├── storage/                        # 저장 계층
│   │   ├── connection_pool.h           # MariaDB 커넥션 풀 (RAII, 4개 연결)
│   │   ├── dao.h                       # 테이블별 DAO (Inspection/Assembly/Model/User/Stats)
│   │   └── password_hash.h             # bcrypt 비밀번호 해시/검증
│   │
│   ├── session/                        # GUI 클라이언트 세션 관리
│   │   ├── session_manager.h           # 세션 등록/해제/broadcast
│   │   ├── gui_tcp_listener.h          # TCP 수신만 담당 (포트 9010)
│   │   ├── gui_router.h               # protocol_no별 분기 → GuiService 호출
│   │   ├── gui_service.h              # 비즈니스 로직 (DAO 호출 + 결과 반환)
│   │   └── gui_notifier.h              # 이벤트 → SessionManager를 통해 클라이언트 push
│   │
│   └── monitor/                        # 모니터링
│       ├── health_checker.h            # 5초 ping/pong, 3회 실패 시 SERVER_DOWN
│       └── connection_registry.h       # sender_addr → fd 매핑 (추론서버 ACK 회신용)
│
└── src/
    ├── core/
    │   ├── main.cpp                    # 진입점 (config.json 로드 + 컴포넌트 DI)
    │   ├── event_bus.cpp               # 큐 상한 10000, backpressure 대응
    │   ├── tcp_listener.cpp            # IP 화이트리스트 + TCP keepalive 튜닝
    │   └── config.cpp                  # ⭐ JSON 스택 파서 (v0.7.0+)
    ├── handler/
    │   ├── router.cpp
    │   ├── station_handler.cpp
    │   ├── train_handler.cpp
    │   └── ack_sender.cpp
    ├── service/
    │   ├── inspection_service.cpp
    │   └── train_service.cpp
    ├── storage/
    │   ├── connection_pool.cpp
    │   ├── dao.cpp
    │   └── password_hash.cpp
    ├── session/
    │   ├── session_manager.cpp
    │   ├── gui_tcp_listener.cpp        # TCP 수신만
    │   ├── gui_router.cpp              # 프로토콜 라우팅 + JSON 응답
    │   ├── gui_service.cpp             # DB 조회/저장 로직
    │   └── gui_notifier.cpp
    └── monitor/
        ├── health_checker.cpp
        └── connection_registry.cpp
```

### 빌드

```
cd MainServer
mkdir build && cd build
cmake ..
cmake --build .
./factory_main_server
```

CMake가 없으면 직접:

```
g++ -std=c++17 -O2 -Iinclude -Icommon \
  src/core/*.cpp src/handler/*.cpp src/storage/*.cpp \
  src/session/*.cpp src/monitor/*.cpp \
  -lpthread -o factory_main_server
```

Windows(MFC와 동일 환경)에서는 `ws2_32.lib` 링크가 필요하며,
`tcp_listener.cpp` / `health_checker.cpp` 상단의 `_WIN32` 분기가 자동 적용됩니다.

## AiServer (Python)

```
AiServer/
├── Common/
│   ├── ConfigLoader.py         # ⭐ config.json 통합 로더 (v0.7.0+)
│   ├── Config.py               # StationConfig dataclass + from_json() 팩토리
│   ├── Protocol.py             # ProtocolNo IntEnum (C++와 동기화)
│   ├── Packet.py               # PacketBuilder (protocol_no/inspection_id 자동 주입)
│   ├── TcpClient.py            # send_with_ack / 원자적 모델 저장 (fsync + rename)
│   ├── SerialCtrl.py           # Arduino 시리얼 (골격)
│   ├── Inferencer.py           # Station1/2Inferencer (PatchCore / YOLO11+PatchCore)
│   └── StationRunner.py        # 비동기 큐 파이프라인 + OK카운트 reporter + INSPECT_META 송신
├── Station1/
│   └── Station1Main.py         # 입고 검사 진입점 (config.json 자동 로드)
├── Station2/
│   └── Station2Main.py         # 조립 검사 진입점 (config.json 자동 로드)
├── Training/
│   ├── TrainingMain.py         # 학습 서버 진입점 (station_id/model_type 필수 포함)
│   ├── TrainingConfig.py       # 학습 설정 dataclass + from_json() 팩토리
│   ├── TrainPatchcore.py       # PatchCore 비지도 학습 (anomalib)
│   └── TrainYolo.py            # YOLO11 전이학습 (ultralytics)
└── tests/
    ├── TestPipeline.py         # ⭐ 실제 NG/OK/META 송신 시뮬레이션 (통합 테스트)
    ├── TestInference.py        # 추론 테스트 스크립트
    ├── TestTraining.py         # 학습 테스트 스크립트
    ├── DebugInference.py       # 추론 디버깅
    ├── TestBatchInference.py   # 배치 추론 테스트
    └── GenerateDummyData.py    # 더미 이미지 데이터 생성
```

### 실행

```
cd AiServer
source .venv/bin/activate              # venv 활성화 (권장)
python -m Station1.Station1Main        # 입고
python -m Station2.Station2Main        # 조립
python -m Training.TrainingMain        # 학습 서버
```

### venv 환경 준비 (Ubuntu 24.04+ PEP 668 대응)

```
cd AiServer
python3 -m venv .venv
source .venv/bin/activate
pip install numpy opencv-python pillow scikit-learn timm einops
pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
```

실제 운영 시 추가 설치:

```
pip install pypylon pyserial ultralytics anomalib
```

## MFC Client (Windows)

```
client/
├── Factory_UI_CL.slnx                 # Visual Studio 솔루션
├── Factory_UI_CL/
│   ├── ClientConfig.h/cpp             # ⭐ config.json 로더 (v0.7.0+)
│   ├── ClientProtocol.h               # 프로토콜 번호 + fallback 기본값
│   ├── PacketBuilder.h/cpp            # JSON 빌더 (ExtractBool, Utf8ToWide, EscapeJson)
│   ├── NetworkClient.h/cpp            # TCP 통신 + silent drop 감지
│   ├── MainTabDlg.h/cpp               # 5개 탭 + IDT_STATUSBAR 실시간 동기화
│   ├── LoginDlg.h/cpp                 # 로그인/회원가입 (서버 DB 인증)
│   ├── PageHome/Station1/Station2/Stats/Model.cpp
│   └── ...
└── tests/
    └── TestPacketBuilder.exe
```
