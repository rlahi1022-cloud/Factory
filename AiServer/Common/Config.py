"""Config.py
간단한 설정 컨테이너. YAML/JSON 파싱은 yaml 의존성을 피하기 위해 dict 기반.
실제 운영에서는 PyYAML 로 Config.yaml 로드 권장.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class StationConfig:
    """스테이션 단위 설정."""

    station_id: int = 1
    main_server_host: str = "127.0.0.1"
    main_server_port: int = 9000

    # Pylon 카메라 설정
    camera_serial: str = ""

    # 모델 경로
    model_path: str = ""                          # PatchCore 모델 (Station1) 또는 YOLO11 모델 (Station2)
    patchcore_model_path: str = ""                # Station2 전용: 라벨 표면 PatchCore 모델 경로

    # PatchCore 이상 점수 임계값
    anomaly_threshold: float = 0.5                # 이상 점수 > threshold → NG

    # YOLO11 설정 (Station2)
    yolo_conf_threshold: float = 0.5              # YOLO 탐지 신뢰도 임계값
    yolo_iou_threshold: float = 0.45              # YOLO NMS IoU 임계값
    yolo_input_size: int = 640                    # YOLO 입력 크기
    patchcore_input_size: int = 224               # PatchCore 입력 크기

    # 워커/큐 설정
    grab_queue_max: int = 16                      # 카메라 grab 결과를 담는 큐 사이즈
    inference_workers: int = 1                    # 추론 코루틴 수
    sender_workers: int = 1                       # 메인서버 송신 코루틴 수

    # Arduino 시리얼
    arduino_port: Optional[str] = None
    arduino_baud: int = 9600
