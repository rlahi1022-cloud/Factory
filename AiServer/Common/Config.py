"""Config.py — 추론 서버 설정 파일

이 파일은 AI 추론 서버(Station1, Station2)가 동작하는 데 필요한
모든 설정값을 하나의 데이터 클래스(dataclass)로 관리한다.
서버 IP, 모델 경로, 임계값, 큐 크기, 아두이노 포트 등을 포함한다.

실제 운영에서는 Config.yaml 파일을 읽어서 이 클래스에 채우는 방식을 권장한다.
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리 (Python 3.10 미만 호환)

from dataclasses import dataclass, field  # dataclass: 클래스를 간결하게 정의하는 데코레이터
from typing import Optional  # Optional: 값이 None일 수도 있음을 표현하는 타입


@dataclass  # 이 데코레이터를 붙이면 __init__, __repr__ 등이 자동 생성된다
class StationConfig:
    """스테이션(추론서버) 단위 설정을 담는 클래스.

    각 필드에 기본값이 지정되어 있어, 필요한 것만 바꿔서 사용할 수 있다.
    예: StationConfig(station_id=1, device="cpu")
    """

    # ── 기본 서버 정보 ──
    station_id: int = 1                           # 이 추론서버의 스테이션 번호 (1=입고검사, 2=조립검사)
    main_server_host: str = "10.10.10.130"        # 운용서버(메인서버)의 IP 주소
    main_server_port: int = 9000                  # 운용서버의 TCP 포트 번호

    # ── Pylon 카메라 설정 ──
    camera_serial: str = ""                       # Basler Pylon 카메라 시리얼 번호 (빈 문자열이면 첫 번째 카메라 사용)

    # ── AI 모델 경로 ──
    model_path: str = ""                          # 메인 모델 파일 경로 (Station1: PatchCore .ckpt / Station2: YOLO11 .pt)
    patchcore_model_path: str = ""                # Station2 전용: 라벨 표면 품질 검사용 PatchCore 모델 경로

    # ── 추론 디바이스 설정 ──
    # "auto" : GPU가 있으면 GPU(cuda), 없으면 CPU 자동 선택
    # "cuda" : GPU 강제 사용 (없으면 CPU로 fallback)
    # "cpu"  : CPU 강제 사용
    device: str = "auto"

    # ── PatchCore 이상탐지 설정 ──
    anomaly_threshold: float = 0.5                # 이상 점수(anomaly score)가 이 값을 넘으면 NG(불량) 판정
    patchcore_input_size: int = 224               # PatchCore 모델 입력 이미지 크기 (224x224 픽셀)

    # ── YOLO11 객체탐지 설정 (Station2 전용) ──
    yolo_conf_threshold: float = 0.5              # YOLO 탐지 신뢰도 임계값 (이 값 이상인 탐지만 유효)
    yolo_iou_threshold: float = 0.45              # YOLO NMS(Non-Maximum Suppression)에서 겹치는 박스 제거 기준
    yolo_input_size: int = 640                    # YOLO 모델 입력 이미지 크기 (640x640 픽셀)

    # ── 비동기 큐/워커 설정 ──
    grab_queue_max: int = 16                      # 카메라에서 캡처한 이미지를 담는 큐의 최대 크기
    inference_workers: int = 1                    # 동시에 추론을 수행하는 워커(코루틴) 수
    sender_workers: int = 1                       # 메인서버로 결과를 전송하는 워커(코루틴) 수

    # ── Arduino 시리얼 통신 설정 ──
    arduino_port: Optional[str] = None            # 아두이노 시리얼 포트 (예: "COM3", "/dev/ttyUSB0", None이면 미사용)
    arduino_baud: int = 9600                      # 아두이노 시리얼 통신 속도 (baud rate)
