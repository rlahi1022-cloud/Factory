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

    # Pylon 카메라 설정 (placeholder)
    camera_serial: str = ""

    # 모델 경로 (실제 모델 파일은 본 골격에서는 사용하지 않음)
    model_path: str = ""

    # 워커/큐 설정
    grab_queue_max: int = 16          # 카메라 grab 결과를 담는 큐 사이즈
    inference_workers: int = 1        # 추론 코루틴 수
    sender_workers: int = 1           # 메인서버 송신 코루틴 수

    # Arduino 시리얼
    arduino_port: Optional[str] = None
    arduino_baud: int = 9600
