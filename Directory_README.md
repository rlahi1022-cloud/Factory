# 디렉터리 구조

```
Factory/
├── MainServer/       # 메인 운영 서버 (C++17, 이벤트 버스 아키텍처)
└── AiServer/         # AI 추론 서버 (Python 3.10+, asyncio.Queue 비동기 큐)
```

학습 서버는 `AiServer/Training/` 디렉터리에 포함되어 있으며,
학습 완료된 가중치(`*.ckpt`, `*.pt`)는 TCP 바이너리 전송으로
메인서버(130)를 경유하여 추론서버에 배포됩니다 (MODEL_RELOAD_CMD).

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
│   │   ├── event_bus.h                 # 워커 풀(4스레드) 기반 이벤트 버스
│   │   ├── event_types.h               # EventType enum + 페이로드 struct
│   │   ├── logger.h                    # 상태 중심 로그 유틸리티 (이모지 + 한글)
│   │   └── tcp_listener.h              # 추론서버용 TCP 리스너 (포트 9000)
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
│   │   ├── gui_tcp_listener.h          # MFC 클라이언트 전용 TCP 리스너 (포트 9010)
│   │   └── gui_notifier.h              # 이벤트 → SessionManager를 통해 클라이언트 push
│   │
│   └── monitor/                        # 모니터링
│       ├── health_checker.h            # 5초 ping/pong, 3회 실패 시 SERVER_DOWN
│       └── connection_registry.h       # sender_addr → fd 매핑 (추론서버 ACK 회신용)
│
└── src/
    ├── core/
    │   ├── main.cpp                    # 진입점, 컴포넌트 생성 및 구동
    │   ├── event_bus.cpp
    │   └── tcp_listener.cpp
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
    │   ├── gui_tcp_listener.cpp
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
│   ├── Config.py               # StationConfig dataclass
│   ├── Protocol.py             # ProtocolNo IntEnum (C++와 동기화)
│   ├── Packet.py               # PacketBuilder (protocol_no/inspection_id 자동 주입)
│   ├── TcpClient.py            # send_with_ack / send_fire_and_forget
│   ├── SerialCtrl.py           # Arduino 시리얼 (골격)
│   ├── Inferencer.py           # Station1/2Inferencer (PatchCore / YOLO11+PatchCore)
│   └── StationRunner.py        # 비동기 큐 파이프라인 + OK카운트 reporter + INSPECT_META 송신
├── Station1/
│   └── Station1Main.py         # 입고 검사 진입점
├── Station2/
│   └── Station2Main.py         # 조립 검사 진입점
├── Training/
│   ├── TrainingMain.py         # 학습 서버 진입점 (TCP 포트 9100)
│   ├── TrainingConfig.py       # 학습 설정 dataclass
│   ├── TrainPatchcore.py       # PatchCore 비지도 학습 (anomalib)
│   └── TrainYolo.py            # YOLO11 전이학습 (ultralytics)
└── tests/
    ├── test_inference.py       # 추론 테스트 스크립트
    ├── test_training.py        # 학습 테스트 스크립트
    └── generate_dummy_data.py  # 더미 이미지 데이터 생성
```

### 실행

```
cd AiServer
python -m Station1.Station1Main      # 입고
python -m Station2.Station2Main      # 조립
```

별도 의존성 없이 즉시 실행됩니다 (모델은 placeholder, 카메라는 더미 0.5초 주기).

실제 운영 시 추가 설치:

```
pip install pypylon opencv-python pyserial torch ultralytics anomalib
```
