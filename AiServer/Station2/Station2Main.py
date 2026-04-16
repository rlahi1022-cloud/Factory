"""Station2Main.py
조립 검사 (Station 2) AI 추론 서버 진입점.

YOLO11 + PatchCore 하이브리드 추론으로 캡/라벨 조립 완성도를 검사한다.
- 1차: YOLO11 — cap, label, liquid_level 존재 여부 + IoU 기반 위치 판정
- 2차: PatchCore — 라벨 표면 품질 이상탐지
- 둘 중 하나라도 NG이면 최종 NG

실행:
  cd Factory/AiServer
  python -m Station2.Station2Main
"""

# ---------------------------------------------------------------------------
# [임포트 영역] 이 파일이 동작하기 위해 필요한 외부 모듈들을 불러온다.
# ---------------------------------------------------------------------------

# __future__.annotations: 타입 힌트(예: -> None)를 문자열로 지연 평가하게 만든다.
# Python 3.10 미만에서도 최신 타입 힌트 문법을 쓸 수 있도록 호환성을 확보하는 역할이다.
from __future__ import annotations

# asyncio: 파이썬의 비동기(async/await) 프로그래밍 프레임워크이다.
# 카메라 영상 수집, YOLO 추론, PatchCore 추론, 결과 전송 등 여러 작업을
# 동시에(비동기로) 처리하기 위해 필요하다.
# Station2는 YOLO + PatchCore 2단계 추론이므로 비동기 처리가 더욱 중요하다.
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

# Station2Inferencer: Station2 전용 AI 추론(inference) 엔진이다.
# YOLO11로 객체 탐지(cap, label, liquid_level)를 수행하고,
# PatchCore로 라벨 표면의 이상탐지를 수행하는 하이브리드(2단계) 추론 클래스이다.
# YOLO는 '무엇이 어디에 있는가'를 찾고, PatchCore는 '표면이 정상인가'를 판단한다.
from Common.Inferencer import Station2Inferencer

# StationRunner: 카메라 영상 수집 -> AI 추론 -> 결과 전송 파이프라인을 총괄하는 클래스이다.
# Station1과 동일한 Runner를 재사용한다 — inferencer만 다르게 주입하면 되기 때문이다.
# 이것이 '의존성 주입(Dependency Injection)' 패턴의 장점이다.
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
    """Station2 AI 추론 서버의 메인 함수.

    목적:
        설정(config) 생성 -> 추론 엔진 초기화 -> 파이프라인 실행 -> 종료 대기
        Station2는 YOLO11 + PatchCore 하이브리드 추론을 사용하므로,
        두 모델에 대한 설정(경로, 임계값, 입력 크기)을 모두 지정해야 한다.

    매개변수: 없음
    반환값:   없음 (None) — 서버가 종료 신호를 받을 때까지 계속 실행된다.
    """

    # -----------------------------------------------------------------------
    # [설정 객체 생성] Station2에 필요한 모든 설정값을 한 곳에 모은다.
    # Station2는 YOLO + PatchCore 두 모델을 사용하므로 Station1보다 설정이 많다.
    # -----------------------------------------------------------------------

    # StationConfig 객체를 생성한다. 각 매개변수의 역할은 아래와 같다.
    config = StationConfig(

        # station_id: 이 스테이션의 고유 번호. 2번은 '조립 검사' 공정을 뜻한다.
        # 메인 서버에 결과를 보낼 때, 어느 공정에서 온 데이터인지 구분하는 데 쓰인다.
        station_id=2,

        # main_server_host: 검사 결과를 수신하는 메인 서버의 IP 주소이다.
        # "127.0.0.1"은 '자기 자신(로컬호스트)'을 의미한다.
        # 실제 공장 배포 시에는 메인 서버의 실제 IP로 변경해야 한다.
        main_server_host="10.10.10.130",

        # main_server_port: 메인 서버가 대기(listen)하고 있는 포트 번호이다.
        # 포트는 하나의 IP에서 여러 서비스를 구분하는 '문 번호' 같은 것이다.
        # Station1과 동일한 9000번 포트를 사용하여 같은 메인 서버로 결과를 보낸다.
        main_server_port=9000,

        # camera_serial: 산업용 카메라의 시리얼 번호이다.
        # Station2는 Station1과 다른 카메라를 사용할 수 있으므로 별도로 지정한다.
        # 빈 문자열("")이면 자동으로 첫 번째 카메라를 선택한다.
        camera_serial="",

        # model_path: YOLO11 객체 탐지 모델 파일(.pt)의 경로이다.
        # .pt는 PyTorch 모델 파일 형식이다.
        # YOLO11은 이미지에서 cap(뚜껑), label(라벨), liquid_level(수위) 등의
        # 객체를 탐지(detection)하여, 조립 부품이 올바르게 있는지 확인한다.
        model_path="./models/station2_yolo11.pt",               # YOLO11 모델

        # patchcore_model_path: PatchCore 이상탐지 모델 파일(.ckpt)의 경로이다.
        # YOLO가 라벨 영역을 검출하면, 그 부분을 잘라(crop)내어 PatchCore에 넣는다.
        # PatchCore는 라벨 표면의 스크래치, 인쇄 불량 등 미세 결함을 탐지한다.
        # Station1과 달리, Station2는 이 2차 모델도 활성화해야 하므로 경로를 지정한다.
        patchcore_model_path="./models/station2_patchcore.ckpt", # 라벨 표면 PatchCore

        # device: AI 모델이 연산을 수행할 하드웨어를 지정한다.
        #   "auto" -> GPU(CUDA)가 있으면 GPU, 없으면 CPU를 자동 선택
        #   "cuda" -> NVIDIA GPU 강제 사용 (없으면 에러)
        #   "cpu"  -> CPU만 사용 (GPU가 있어도 무시)
        # Station2는 YOLO + PatchCore 두 모델을 돌리므로 GPU 사용이 더욱 권장된다.
        device="auto",                     # "auto" / "cuda" / "cpu"

        # anomaly_threshold: PatchCore의 이상(anomaly) 점수 임계값이다.
        # PatchCore가 매긴 점수가 이 값(0.5)보다 높으면 라벨 표면 '불량(NG)'으로 판정한다.
        # Station1과 동일한 역할이며, 현장 테스트로 최적값을 조정해야 한다.
        anomaly_threshold=0.5,             # PatchCore 이상 점수 임계값

        # yolo_conf_threshold: YOLO 모델의 탐지 신뢰도(confidence) 임계값이다.
        # YOLO가 객체를 탐지할 때 '이것이 cap일 확률은 0.85'처럼 신뢰도 점수를 매기는데,
        # 이 값(0.5) 미만이면 탐지 결과를 무시한다.
        # 낮추면 더 많이 탐지하지만 오탐(false positive)이 늘고,
        # 높이면 정확하지만 미탐(false negative) 위험이 있다.
        yolo_conf_threshold=0.5,           # YOLO 탐지 신뢰도 임계값

        # yolo_iou_threshold: YOLO의 NMS(Non-Maximum Suppression) IoU 임계값이다.
        # NMS는 겹치는 탐지 박스를 하나로 합치는 후처리 과정이다.
        # IoU(Intersection over Union)는 두 박스의 겹침 정도를 0~1로 나타낸다.
        # 0.45 이상 겹치면 같은 객체로 보고 신뢰도가 낮은 박스를 제거한다.
        yolo_iou_threshold=0.45,           # YOLO NMS IoU 임계값

        # yolo_input_size: YOLO 모델에 입력할 이미지의 가로/세로 크기(픽셀)이다.
        # YOLO는 보통 정사각형 입력을 받으며, 640x640이 YOLOv8/v11의 표준 크기이다.
        # 크기가 클수록 작은 객체도 잘 탐지하지만, 연산 시간이 늘어난다.
        yolo_input_size=640,               # YOLO 입력 크기

        # patchcore_input_size: PatchCore 모델에 입력할 이미지의 가로/세로 크기(픽셀)이다.
        # YOLO가 검출한 라벨 영역을 이 크기로 리사이즈(resize)한 후 PatchCore에 넣는다.
        # 224는 ImageNet 기반 모델에서 가장 흔히 쓰이는 표준 크기이다.
        patchcore_input_size=224,          # PatchCore 입력 크기

        # grab_queue_max: 카메라에서 촬영한 이미지를 임시 저장하는 큐(대기열)의 최대 크기이다.
        # 큐가 가득 차면 새 이미지는 버려진다(오래된 이미지가 쌓이는 것을 방지).
        # 16이면 최대 16장까지 대기열에 쌓을 수 있다.
        grab_queue_max=16,

        # inference_workers: 동시에 AI 추론을 수행하는 작업자(worker) 수이다.
        # Station2는 YOLO + PatchCore 2단계 추론이지만, 순차적으로 실행되므로
        # worker 1개가 두 모델을 모두 처리한다. GPU가 여러 개이면 늘릴 수 있다.
        inference_workers=1,

        # sender_workers: 추론 결과를 메인 서버로 전송하는 작업자 수이다.
        # 네트워크 전송은 비교적 빠르므로 1이면 충분한 경우가 많다.
        sender_workers=1,

        # arduino_port: 아두이노(Arduino) 보드와 시리얼 통신할 포트 이름이다.
        # 아두이노는 불량 판정 결과에 따라 물리적 장치(예: 솔레노이드, LED)를 제어한다.
        # None이면 아두이노 연결을 사용하지 않는다.
        # Station2는 Station1과 다른 아두이노를 사용할 수 있으므로 포트가 다를 수 있다.
        arduino_port=None,                 # 예: "COM4" 또는 "/dev/ttyUSB1"

        # arduino_baud: 아두이노와의 시리얼 통신 속도(baud rate)이다.
        # 9600은 아두이노 기본값이며, 아두이노 코드와 동일하게 맞춰야 통신이 된다.
        # 단위는 bps(bits per second)로, 초당 전송 비트 수를 의미한다.
        arduino_baud=9600,
    )

    # -----------------------------------------------------------------------
    # [추론 엔진 & 파이프라인 생성] 설정을 기반으로 핵심 객체들을 만든다.
    # -----------------------------------------------------------------------

    # Station2Inferencer: 위에서 만든 config를 받아 YOLO11 모델과 PatchCore 모델을
    # 둘 다 메모리에 로드한다. 이미지를 넣으면 1차(YOLO) -> 2차(PatchCore) 순서로
    # 추론을 수행하고, 종합 판정 결과(OK/NG)를 반환한다.
    inferencer = Station2Inferencer(config)

    # StationRunner: inferencer를 내부에 품고, 카메라 촬영 -> 추론 -> 결과 전송의
    # 전체 파이프라인을 비동기로 실행하는 관리자(runner) 객체이다.
    # Station1과 동일한 StationRunner 클래스를 재사용한다 (inferencer만 다름).
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
    # runner.run()은 카메라 촬영 -> YOLO 추론 -> PatchCore 추론 -> 결과 전송을
    # 무한 반복하는 메인 루프이다.
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
