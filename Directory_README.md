# 디렉터리 구조

```
Factory/
├── MainServer/       # 메인 운영 서버 (C++17, 이벤트 버스 아키텍처)
└── AiServer/         # AI 추론 서버 (Python 3.10+, asyncio.Queue 비동기 큐)
```

학습 서버는 본 저장소에 포함되지 않으며, 학습 완료된 가중치(`*.ckpt`, `*.pt`)는
별도 경로(SCP/공유폴더)로 추론 서버에 배포되어 `Common/Inferencer.py` 내
`load_model()` / `infer()` 구현에 주입됩니다.

## 명명 규칙

| 대상     | 규칙       | 예시 |
|----------|------------|------|
| 파일명   | PascalCase | `StationRunner.py`, `EventBus.cpp` |
| 클래스명 | PascalCase | `Station1Inferencer`, `PacketBuilder` |
| 변수명   | snake_case | `anomaly_score`, `result_dict` |
| 메소드명 | snake_case | `build_packet()`, `infer_image()` |

## MainServer (C++)

```
MainServer/
├── CMakeLists.txt
├── common/
│   └── Protocol.h              # 메시지 번호 enum(ProtocolNo), ACK 매핑, PROTOCOL_VERSION
├── include/
│   ├── EventTypes.h            # EventType enum + 페이로드 struct
│   ├── EventBus.h              # std::function 기반 자체 이벤트 버스
│   ├── TcpListener.h           # 포트 9000 리슨
│   ├── ConnectionRegistry.h    # ★신규 sender_addr → fd 매핑 (ACK 회신용)
│   ├── Router.h                # protocol_no 기반 분기
│   ├── StationHandler.h        # Station1/2Handler
│   ├── DbManager.h             # MariaDB INSERT (TODO)
│   ├── ImageStorage.h          # NG 이미지 파일 저장
│   ├── GuiNotifier.h           # MFC 클라이언트 푸시 (TODO)
│   ├── HealthChecker.h         # 5초 ping/pong, 3회 실패 판정
│   └── AckSender.h             # ★신규 ACK/NACK 회신
└── src/
    └── (각 *.cpp)
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
  src/*.cpp -lpthread -o factory_main_server
```

Windows(MFC와 동일 환경)에서는 `ws2_32.lib` 링크가 필요하며,
`TcpListener.cpp` / `HealthChecker.cpp` 상단의 `_WIN32` 분기가 자동 적용됩니다.

## AiServer (Python)

```
AiServer/
├── Common/
│   ├── Config.py               # StationConfig dataclass
│   ├── Protocol.py             # ★신규 ProtocolNo IntEnum (C++와 동기화)
│   ├── Packet.py               # PacketBuilder (protocol_no/inspection_id 자동 주입)
│   ├── TcpClient.py            # send_with_ack / send_fire_and_forget
│   ├── SerialCtrl.py           # Arduino 시리얼 (골격)
│   ├── Inferencer.py           # Station1/2Inferencer (모델 자리만)
│   └── StationRunner.py        # 비동기 큐 파이프라인 + OK카운트 reporter + INSPECT_META 송신
├── Station1/
│   └── Station1Main.py         # 입고 검사 진입점
└── Station2/
    └── Station2Main.py         # 조립 검사 진입점
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
