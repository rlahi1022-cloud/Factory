"""TrainingConfig.py
AI 학습 서버 전용 설정.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class TrainingConfig:
    """학습 서버 설정."""

    # TCP 서버 (운용서버가 접속해옴)
    listen_host: str = "0.0.0.0"
    listen_port: int = 9100

    # 운용 서버 주소 (학습 완료/진행 알림 송신용)
    main_server_host: str = "127.0.0.1"
    main_server_port: int = 9000

    # GPU 설정
    device: str = "cuda"             # "cuda" 또는 "cpu"
    gpu_id: int = 0

    # 데이터 경로
    data_root: str = "./data"        # 학습 데이터 루트
    model_output_dir: str = "./models"  # 학습된 모델 저장 경로

    # PatchCore 학습 설정
    patchcore_backbone: str = "wide_resnet50_2"
    patchcore_input_size: int = 224
    patchcore_batch_size: int = 32
    patchcore_num_workers: int = 4

    # YOLO11 학습 설정
    yolo_base_model: str = "yolo11n.pt"   # 사전학습 모델
    yolo_input_size: int = 640
    yolo_epochs: int = 100
    yolo_batch_size: int = 16
    yolo_patience: int = 20               # 조기 종료 patience

    # 데이터 증강
    augmentation_factor: int = 5          # 수집량 대비 증강 배수

    # 모델 배포 경로 (추론서버로 전송할 공유 폴더)
    deploy_dir: str = "./deploy"
