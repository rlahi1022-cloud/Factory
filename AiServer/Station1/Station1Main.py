"""Station1Main.py
입고 검사 (Station 1) AI 추론 서버 진입점.

PatchCore 이상탐지로 빈 페트병(삼다수) 외관 결함을 탐지한다.
결함 유형: 크랙, 이물질, 오염, 파손 등.

실행:
  cd Factory/AiServer
  python -m Station1.Station1Main
"""

# ---------------------------------------------------------------------------
# [임포트 영역] 이 파일이 동작하기 위해 필요한 외부 모듈들을 불러온다.
# ---------------------------------------------------------------------------

# __future__.annotations: 타입 힌트(예: -> None)를 문자열로 지연 평가하게 만든다.
# Python 3.10 미만에서도 최신 타입 힌트 문법을 쓸 수 있도록 호환성을 확보하는 역할이다.
from __future__ import annotations

# asyncio: 파이썬의 비동기(async/await) 프로그래밍 프레임워크이다.
# 카메라 영상 수집, AI 추론, 결과 전송 등 여러 작업을 동시에(비동기로) 처리하기 위해 필요하다.
# 동기(synchronous) 방식이면 한 작업이 끝날 때까지 다음 작업이 멈추므로, 실시간 검사에 부적합하다.
import asyncio

# logging: 프로그램 실행 중 발생하는 정보/경고/에러 메시지를 기록하는 모듈이다.
# print() 대신 logging을 쓰면 메시지 수준(INFO, WARNING, ERROR)을 구분할 수 있고,
# 나중에 파일로 저장하거나 필터링하기도 쉽다. 서버 운영 시 디버깅에 필수적이다.
import logging

# signal: 운영체제가 보내는 '종료 신호(시그널)'를 감지하는 모듈이다.
# 예를 들어 터미널에서 Ctrl+C를 누르면 SIGINT 신호가 발생하는데,
# 이 신호를 잡아서 서버를 안전하게(graceful) 종료하는 데 사용한다.
import signal

# StationConfig: 스테이션(검사 공정)의 모든 설정값을 하나의 객체로 묶어주는 클래스이다.
# IP 주소, 포트 번호, 모델 경로, 임계값 등 설정이 많은데, 이를 딕셔너리 대신
# 클래스로 관리하면 오타 방지 및 자동완성 등의 이점이 있다.
from Common.Config import StationConfig

# Station1Inferencer: Station1 전용 AI 추론(inference) 엔진이다.
# PatchCore 모델을 로드하고, 입력 이미지에 대해 정상/불량 판정을 수행하는 핵심 클래스이다.
# 추론(Inference)이란 학습된 모델에 새 데이터를 넣어 예측 결과를 얻는 과정을 뜻한다.
from Common.Inferencer import Station1Inferencer

# StationRunner: 카메라 영상 수집 -> AI 추론 -> 결과 전송 파이프라인을 총괄하는 클래스이다.
# 내부적으로 비동기 큐(Queue)를 사용해 각 단계를 병렬로 처리한다.
# 이 클래스 덕분에 Station1Main.py는 설정과 실행만 담당하면 되어 코드가 간결해진다.
from Common.StationRunner import StationRunner


# ---------------------------------------------------------------------------
# [로깅 설정] 프로그램 전체에서 사용할 로그 출력 형식을 지정한다.
# ---------------------------------------------------------------------------

# logging.basicConfig(): 로깅 시스템의 기본 설정을 초기화하는 함수이다.
#   - level=logging.INFO : INFO 이상 수준의 메시지만 출력한다 (DEBUG는 무시).
#     로그 수준 순서: DEBUG < INFO < WARNING < ERROR < CRITICAL
#   - format="..." : 로그 한 줄의 출력 형식을 지정한다.
#     %(asctime)s   -> 시간 (예: 2025-01-15 14:30:00,123)
#     %(levelname)s -> 수준 (예: INFO, ERROR)
#     %(name)s      -> 로거 이름 (어느 모듈에서 찍었는지 구분)
#     %(message)s   -> 실제 로그 메시지
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)


# ---------------------------------------------------------------------------
# [메인 비동기 함수] 서버의 전체 생명주기를 관리한다.
# ---------------------------------------------------------------------------

async def main() -> None:
    """Station1 AI 추론 서버의 메인 함수.

    목적:
        설정(config) 생성 -> 추론 엔진 초기화 -> 파이프라인 실행 -> 종료 대기
        이 함수 하나가 Station1 서버의 '시작부터 종료까지' 전 과정을 담당한다.

    매개변수: 없음
    반환값:   없음 (None) — 서버가 종료 신호를 받을 때까지 계속 실행된다.
    """

    # -----------------------------------------------------------------------
    # [설정 객체 생성] Station1에 필요한 모든 설정값을 한 곳에 모은다.
    # -----------------------------------------------------------------------

    # StationConfig 객체를 생성한다. 각 매개변수의 역할은 아래와 같다.
    config = StationConfig(

        # station_id: 이 스테이션의 고유 번호. 1번은 '입고 검사' 공정을 뜻한다.
        # 메인 서버에 결과를 보낼 때, 어느 공정에서 온 데이터인지 구분하는 데 쓰인다.
        station_id=1,

        # main_server_host: 검사 결과를 수신하는 메인 서버의 IP 주소이다.
        # "127.0.0.1"은 '자기 자신(로컬호스트)'을 의미한다.
        # 실제 공장 배포 시에는 메인 서버의 실제 IP로 변경해야 한다.
        main_server_host="10.10.10.130",

        # main_server_port: 메인 서버가 대기(listen)하고 있는 포트 번호이다.
        # 포트는 하나의 IP에서 여러 서비스를 구분하는 '문 번호' 같은 것이다.
        # 메인 서버와 같은 포트(9000)를 맞춰야 통신이 된다.
        main_server_port=9000,

        # camera_serial: 산업용 카메라의 시리얼 번호이다.
        # 여러 대의 카메라가 연결된 환경에서 특정 카메라를 지정할 때 사용한다.
        # 빈 문자열("")이면 자동으로 첫 번째 카메라를 선택한다.
        camera_serial="",

        # model_path: PatchCore 이상탐지 모델 파일(.ckpt)의 경로이다.
        # .ckpt는 PyTorch Lightning의 체크포인트 파일 형식으로,
        # 학습이 완료된 모델의 가중치(weights)와 설정이 저장되어 있다.
        # 이 모델이 페트병의 정상/불량을 판별하는 핵심 두뇌 역할을 한다.
        model_path="./models/station1_patchcore.ckpt",

        # patchcore_model_path: 2차 PatchCore 모델 경로이다.
        # Station1은 PatchCore 하나만 사용하므로 빈 문자열로 비활성화한다.
        # Station2처럼 YOLO + PatchCore 조합이 필요한 경우에만 이 경로를 지정한다.
        patchcore_model_path="",           # Station1은 사용 안 함

        # device: AI 모델이 연산을 수행할 하드웨어를 지정한다.
        #   "auto" -> GPU(CUDA)가 있으면 GPU, 없으면 CPU를 자동 선택
        #   "cuda" -> NVIDIA GPU 강제 사용 (없으면 에러)
        #   "cpu"  -> CPU만 사용 (GPU가 있어도 무시)
        # GPU를 쓰면 딥러닝 연산이 10~100배 빨라지므로 실시간 검사에 유리하다.
        device="auto",                     # "auto" / "cuda" / "cpu"

        # anomaly_threshold: PatchCore의 이상(anomaly) 점수 임계값이다.
        # PatchCore는 각 이미지에 0~1 사이의 '이상 점수'를 매기는데,
        # 이 값(0.5)보다 높으면 '불량(NG)', 낮으면 '정상(OK)'으로 판정한다.
        # 임계값을 낮추면 불량 감지가 민감해지고(과검출 위험),
        # 높이면 둔감해진다(미검출 위험). 현장 테스트로 최적값을 찾아야 한다.
        anomaly_threshold=0.5,             # PatchCore 이상 점수 임계값

        # patchcore_input_size: PatchCore 모델에 입력할 이미지의 가로/세로 크기(픽셀)이다.
        # 카메라 원본 이미지를 이 크기로 리사이즈(resize)한 후 모델에 넣는다.
        # 224는 ImageNet 기반 모델에서 가장 흔히 쓰이는 표준 크기이다.
        patchcore_input_size=224,

        # grab_queue_max: 카메라에서 촬영한 이미지를 임시 저장하는 큐(대기열)의 최대 크기이다.
        # 큐가 가득 차면 새 이미지는 버려진다(오래된 이미지가 쌓이는 것을 방지).
        # 16이면 최대 16장까지 대기열에 쌓을 수 있다.
        grab_queue_max=16,

        # inference_workers: 동시에 AI 추론을 수행하는 작업자(worker) 수이다.
        # GPU가 1개이면 1로 두는 것이 일반적이다.
        # 여러 GPU가 있을 때 늘리면 처리량(throughput)이 올라간다.
        inference_workers=1,

        # sender_workers: 추론 결과를 메인 서버로 전송하는 작업자 수이다.
        # 네트워크 전송은 비교적 빠르므로 1이면 충분한 경우가 많다.
        sender_workers=1,

        # arduino_port: 아두이노(Arduino) 보드와 시리얼 통신할 포트 이름이다.
        # 아두이노는 불량 판정 결과에 따라 물리적 장치(예: 솔레노이드, LED)를 제어한다.
        # None이면 아두이노 연결을 사용하지 않는다.
        # 실제 연결 시 Windows는 "COM3", Linux는 "/dev/ttyUSB0" 형태로 지정한다.
        arduino_port=None,                 # 예: "COM3" 또는 "/dev/ttyUSB0"

        # arduino_baud: 아두이노와의 시리얼 통신 속도(baud rate)이다.
        # 9600은 아두이노 기본값이며, 아두이노 코드와 동일하게 맞춰야 통신이 된다.
        # 단위는 bps(bits per second)로, 초당 전송 비트 수를 의미한다.
        arduino_baud=9600,
    )

    # -----------------------------------------------------------------------
    # [추론 엔진 & 파이프라인 생성] 설정을 기반으로 핵심 객체들을 만든다.
    # -----------------------------------------------------------------------

    # Station1Inferencer: 위에서 만든 config를 받아 PatchCore 모델을 메모리에 로드한다.
    # 이후 이미지를 넣으면 이상 점수를 반환하는 .infer() 메서드를 제공한다.
    inferencer = Station1Inferencer(config)

    # StationRunner: inferencer를 내부에 품고, 카메라 촬영 -> 추론 -> 결과 전송의
    # 전체 파이프라인을 비동기로 실행하는 관리자(runner) 객체이다.
    runner = StationRunner(config, inferencer)

    # -----------------------------------------------------------------------
    # [종료 신호 처리] Ctrl+C 등으로 서버를 안전하게 멈출 수 있도록 설정한다.
    # -----------------------------------------------------------------------

    # asyncio.get_running_loop(): 현재 실행 중인 이벤트 루프(event loop) 객체를 가져온다.
    # 이벤트 루프란 비동기 작업들을 스케줄링하고 실행하는 asyncio의 핵심 엔진이다.
    # 신호 핸들러를 등록하려면 이벤트 루프 객체가 필요하다.
    loop = asyncio.get_running_loop()

    # asyncio.Event(): 여러 비동기 작업 간에 '신호'를 주고받는 이벤트 객체이다.
    # .set()을 호출하면 '신호가 왔다'는 뜻이 되고,
    # .wait()로 대기 중인 코드가 깨어난다. 여기서는 '서버 종료 신호' 역할을 한다.
    stop_event = asyncio.Event()

    def request_stop() -> None:
        """종료 요청을 처리하는 콜백(callback) 함수.

        목적:
            운영체제 종료 신호(SIGINT, SIGTERM)가 들어오면 호출되어,
            stop_event를 '발생(set)' 상태로 바꾼다.
            이렇게 하면 아래의 await stop_event.wait()가 깨어나면서 종료 절차가 시작된다.

        매개변수: 없음
        반환값:   없음 (None)
        """
        # stop_event를 '발생' 상태로 설정하여 종료 대기 중인 코드를 깨운다.
        stop_event.set()

    # SIGINT(Ctrl+C)와 SIGTERM(kill 명령) 두 가지 종료 신호를 처리한다.
    # 두 신호 모두 잡아야 다양한 종료 상황(수동 중단, 시스템 종료)에 대응할 수 있다.
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            # loop.add_signal_handler(): 특정 OS 신호가 발생하면 지정한 함수를 호출하도록 등록한다.
            # sig(SIGINT 또는 SIGTERM)이 들어오면 request_stop()이 호출된다.
            loop.add_signal_handler(sig, request_stop)
        except NotImplementedError:
            # Windows에서는 add_signal_handler가 지원되지 않아 NotImplementedError가 발생한다.
            # 이 경우 무시(pass)하고, asyncio.run()이 자체적으로 KeyboardInterrupt를 처리한다.
            pass

    # -----------------------------------------------------------------------
    # [서버 실행 & 종료 대기] 파이프라인을 시작하고, 종료 신호가 올 때까지 기다린다.
    # -----------------------------------------------------------------------

    # loop.create_task(): runner.run() 코루틴을 이벤트 루프에 '태스크'로 등록한다.
    # 태스크로 등록하면 백그라운드에서 비동기로 실행되며, 다른 코드도 동시에 진행할 수 있다.
    # runner.run()은 카메라 촬영 -> 추론 -> 결과 전송을 무한 반복하는 메인 루프이다.
    runner_task = loop.create_task(runner.run())

    # stop_event가 .set() 될 때까지 여기서 멈춰서 대기한다.
    # 즉, Ctrl+C 또는 SIGTERM이 들어올 때까지 서버가 계속 동작한다.
    await stop_event.wait()

    # runner.stop(): 파이프라인에 종료 명령을 보낸다.
    # 내부적으로 카메라 연결 해제, 큐 비우기, 네트워크 연결 닫기 등 정리 작업을 수행한다.
    # 이렇게 '안전한 종료(graceful shutdown)'를 해야 데이터 손실이나 리소스 누수를 막을 수 있다.
    await runner.stop()

    # runner_task가 완전히 끝날 때까지 대기한다.
    # stop() 호출 후에도 진행 중인 추론이 있을 수 있으므로, 완료를 보장하기 위해 await 한다.
    await runner_task


# ---------------------------------------------------------------------------
# [스크립트 진입점] 이 파일이 직접 실행될 때만 main()을 호출한다.
# ---------------------------------------------------------------------------

# __name__ == "__main__": 파이썬에서 스크립트가 '직접 실행'된 경우에만 True가 된다.
# 다른 파일에서 import할 때는 False이므로, main()이 자동 실행되지 않는다.
# 이 패턴을 사용하면 모듈 재사용성이 높아진다.
if __name__ == "__main__":
    # asyncio.run(): 비동기 main() 함수를 실행하는 진입점이다.
    # 내부적으로 이벤트 루프를 생성하고, main()이 끝나면 루프를 정리(cleanup)한다.
    # 프로그램 전체에서 asyncio.run()은 보통 한 번만 호출한다.
    asyncio.run(main())
