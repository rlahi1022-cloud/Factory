"""TrainingConfig.py — 학습 서버 설정 (config.json 기반)

프로젝트 루트의 config/config.json에서 학습 관련 설정을 로드한다.
AiServer/Common/ConfigLoader.py를 사용한다.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

# 상위 패키지 접근을 위해 AiServer 디렉터리를 sys.path에 추가
_THIS_DIR = Path(__file__).resolve().parent
_AISERVER_DIR = _THIS_DIR.parent
if str(_AISERVER_DIR) not in sys.path:
    sys.path.insert(0, str(_AISERVER_DIR))

from Common.ConfigLoader import ConfigLoader


@dataclass
class TrainingConfig:
    """학습 서버 설정 데이터 클래스."""

    # TCP
    listen_host: str = "0.0.0.0"
    listen_port: int = 9100
    main_server_host: str = "10.10.10.130"
    main_server_port: int = 9000

    # GPU
    device: str = "cuda"
    gpu_id: int = 0

    # 데이터 경로
    data_root: str = "./data"
    model_output_dir: str = "./models"

    # PatchCore
    patchcore_backbone: str = "wide_resnet50_2"
    patchcore_input_size: int = 224
    patchcore_batch_size: int = 32
    patchcore_num_workers: int = 4

    # YOLO11
    yolo_base_model: str = "yolo11n.pt"
    yolo_input_size: int = 640
    yolo_epochs: int = 100
    yolo_batch_size: int = 16
    yolo_patience: int = 20

    # 증강/배포
    augmentation_factor: int = 5
    deploy_dir: str = "./deploy"

    @classmethod
    def from_json(cls) -> "TrainingConfig":
        """config.json에서 학습 설정을 로드한다."""
        if ConfigLoader._config is None:
            ConfigLoader.load()

        return cls(
            listen_host=ConfigLoader.get("training.listen_host", "0.0.0.0"),
            listen_port=ConfigLoader.get_int("network.training_server_port", 9100),
            main_server_host=ConfigLoader.get("network.main_server_host", "10.10.10.130"),
            main_server_port=ConfigLoader.get_int("network.main_server_ai_port", 9000),

            device=ConfigLoader.get("training.device", "cuda"),
            gpu_id=ConfigLoader.get_int("training.gpu_id", 0),

            data_root=ConfigLoader.get("training.data_root", "./data"),
            model_output_dir=ConfigLoader.get("training.model_output_dir", "./models"),

            patchcore_backbone=ConfigLoader.get("training.patchcore_backbone", "wide_resnet50_2"),
            patchcore_input_size=ConfigLoader.get_int("training.patchcore_input_size", 224),
            patchcore_batch_size=ConfigLoader.get_int("training.patchcore_batch_size", 32),

            yolo_base_model=ConfigLoader.get("training.yolo_base_model", "yolo11n.pt"),
            yolo_input_size=ConfigLoader.get_int("training.yolo_input_size", 640),
            yolo_epochs=ConfigLoader.get_int("training.yolo_epochs", 100),
            yolo_batch_size=ConfigLoader.get_int("training.yolo_batch_size", 16),
            yolo_patience=ConfigLoader.get_int("training.yolo_patience", 20),

            augmentation_factor=ConfigLoader.get_int("training.augmentation_factor", 5),
            deploy_dir=ConfigLoader.get("training.deploy_dir", "./deploy"),
        )
