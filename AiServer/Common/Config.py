"""Config.py — 추론 서버 설정 (config.json 기반)

이 파일은 Common/ConfigLoader.py를 통해 프로젝트 루트의
config/config.json에서 설정을 읽어와 StationConfig 객체를 생성한다.

기존의 하드코딩 dataclass는 유지하되, 값은 config.json에서 로드된다.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from Common.ConfigLoader import ConfigLoader


@dataclass
class StationConfig:
    """스테이션(추론서버) 단위 설정.

    직접 생성보다는 from_json(station_id)으로 config.json에서 로드 권장.
    """

    station_id: int = 1
    main_server_host: str = "10.10.10.130"
    main_server_port: int = 9000

    camera_serial: str = ""

    model_path: str = ""
    patchcore_model_path: str = ""

    device: str = "auto"

    anomaly_threshold: float = 0.5
    patchcore_input_size: int = 224

    yolo_conf_threshold: float = 0.5
    yolo_iou_threshold: float = 0.45
    yolo_input_size: int = 640

    grab_queue_max: int = 16
    inference_workers: int = 1
    sender_workers: int = 1

    arduino_port: Optional[str] = None
    arduino_baud: int = 9600

    @classmethod
    def from_json(cls, station_id: int) -> "StationConfig":
        """config.json에서 스테이션 설정을 로드한다.

        Args:
            station_id: 1 또는 2

        Returns:
            StationConfig 인스턴스 (config.json 값으로 채워짐)
        """
        if ConfigLoader._config is None:
            ConfigLoader.load()

        prefix = f"ai_server.station{station_id}"

        return cls(
            station_id=station_id,
            main_server_host=ConfigLoader.get("network.main_server_host", "10.10.10.130"),
            main_server_port=ConfigLoader.get_int("network.main_server_ai_port", 9000),

            model_path=ConfigLoader.get(f"{prefix}.model_path", ""),
            patchcore_model_path=ConfigLoader.get(f"{prefix}.patchcore_model_path", ""),
            device=ConfigLoader.get(f"{prefix}.device", "auto"),

            anomaly_threshold=ConfigLoader.get_float(f"{prefix}.anomaly_threshold", 0.5),
            patchcore_input_size=ConfigLoader.get_int(f"{prefix}.patchcore_input_size", 224),

            yolo_conf_threshold=ConfigLoader.get_float(f"{prefix}.yolo_conf_threshold", 0.5),
            yolo_iou_threshold=ConfigLoader.get_float(f"{prefix}.yolo_iou_threshold", 0.45),
            yolo_input_size=ConfigLoader.get_int(f"{prefix}.yolo_input_size", 640),

            grab_queue_max=ConfigLoader.get_int(f"{prefix}.grab_queue_max", 16),
            inference_workers=ConfigLoader.get_int(f"{prefix}.inference_workers", 1),
            sender_workers=ConfigLoader.get_int(f"{prefix}.sender_workers", 1),
        )
