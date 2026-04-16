"""TrainPatchcore.py
PatchCore (Anomalib) 학습 파이프라인.

용도:
  - Station1 입고 검사: 빈 용기 외관 결함 이상탐지
  - Station2 조립 검사: 라벨 표면 품질 이상탐지

학습 전략:
  - 정상 이미지만으로 학습 (비지도학습, unsupervised)
  - 백본: 사전학습 ResNet(wide_resnet50_2) 특징 추출기
  - 입력 크기: 224x224
  - 데이터 증강: 회전, 반전, 밝기/대비, Gaussian Noise 등
"""

from __future__ import annotations

import logging
import os
import shutil
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Optional

logger = logging.getLogger(__name__)


class PatchcoreTrainer:
    """PatchCore 학습 파이프라인."""

    def __init__(self,
                 station_id: int,
                 data_dir: str,
                 output_dir: str,
                 backbone: str = "wide_resnet50_2",
                 input_size: int = 224,
                 batch_size: int = 32,
                 num_workers: int = 4,
                 device: str = "cuda",
                 progress_callback: Optional[Callable[[dict], None]] = None):
        self._station_id = station_id
        self._data_dir = Path(data_dir)
        self._output_dir = Path(output_dir)
        self._backbone = backbone
        self._input_size = input_size
        self._batch_size = batch_size
        self._num_workers = num_workers
        self._device = device
        self._progress_callback = progress_callback

    def train(self) -> dict:
        """PatchCore 학습 실행.

        Returns:
            {success, model_path, version, accuracy, message}
        """
        version = datetime.now().strftime("v%Y%m%d_%H%M%S")
        model_name = f"station{self._station_id}_patchcore_{version}"

        try:
            self._report_progress(0, "Initializing PatchCore training...")

            from anomalib.data import Folder
            from anomalib.engine import Engine
            from anomalib.models import Patchcore

            # 데이터셋 구성 (Anomalib Folder 형식)
            # data_dir 구조: normal/ (정상 이미지만)
            normal_dir = self._data_dir / "normal"
            if not normal_dir.exists():
                # data_dir 자체에 이미지가 있으면 그것을 normal로 사용
                normal_dir = self._data_dir

            self._report_progress(10, "Loading dataset...")

            datamodule = Folder(
                name=model_name,
                root=str(self._data_dir),
                normal_dir=str(normal_dir),
                image_size=(self._input_size, self._input_size),
                train_batch_size=self._batch_size,
                eval_batch_size=self._batch_size,
                num_workers=self._num_workers,
                task="classification",
            )

            self._report_progress(20, "Creating PatchCore model...")

            model = Patchcore(
                backbone=self._backbone,
                layers_to_extract=["layer2", "layer3"],
            )

            self._report_progress(30, "Starting PatchCore training (feature extraction)...")

            # Anomalib Engine으로 학습
            save_dir = self._output_dir / model_name
            save_dir.mkdir(parents=True, exist_ok=True)

            engine = Engine(
                default_root_dir=str(save_dir),
                max_epochs=1,  # PatchCore는 1 epoch (메모리 뱅크 구축)
                devices=1,
                accelerator="gpu" if self._device == "cuda" else "cpu",
            )

            self._report_progress(40, "Training PatchCore model...")
            engine.fit(model=model, datamodule=datamodule)

            self._report_progress(70, "Evaluating model...")
            test_results = engine.test(model=model, datamodule=datamodule)

            # 정확도 추출
            accuracy = 0.0
            if test_results and len(test_results) > 0:
                accuracy = test_results[0].get("image_AUROC", 0.0)

            self._report_progress(85, "Saving model checkpoint...")

            # 최종 체크포인트 경로
            ckpt_path = save_dir / "weights" / "lightning" / "model.ckpt"
            final_path = self._output_dir / f"{model_name}.ckpt"

            if ckpt_path.exists():
                shutil.copy2(ckpt_path, final_path)
            else:
                # 대체: 체크포인트 검색
                ckpt_files = list(save_dir.rglob("*.ckpt"))
                if ckpt_files:
                    shutil.copy2(ckpt_files[0], final_path)
                else:
                    raise FileNotFoundError("No checkpoint found after training")

            self._report_progress(100, "Training complete!")

            return {
                "success": True,
                "model_path": str(final_path),
                "version": version,
                "accuracy": round(accuracy, 4),
                "message": f"PatchCore training complete: {model_name}",
            }

        except ImportError as exc:
            msg = f"Required package not installed: {exc}"
            logger.error(msg)
            return {"success": False, "model_path": "", "version": version,
                    "accuracy": 0.0, "message": msg}
        except Exception as exc:
            msg = f"PatchCore training failed: {exc}"
            logger.exception(msg)
            return {"success": False, "model_path": "", "version": version,
                    "accuracy": 0.0, "message": msg}

    def _report_progress(self, progress: int, status: str) -> None:
        """학습 진행 상태 콜백."""
        logger.info("[Station%d PatchCore] %d%% — %s", self._station_id, progress, status)
        if self._progress_callback:
            self._progress_callback({
                "station_id": self._station_id,
                "model_type": "PatchCore",
                "progress": progress,
                "status": status,
            })


def augment_dataset(data_dir: str, factor: int = 5) -> None:
    """데이터 증강 (학습 전 호출).

    적용 증강: 회전(90/180/270), 수평/수직 반전, 밝기/대비, Gaussian Noise, Random Crop.
    """
    import cv2
    import numpy as np

    src_dir = Path(data_dir)
    images = list(src_dir.glob("*.jpg")) + list(src_dir.glob("*.png")) + list(src_dir.glob("*.bmp"))

    if not images:
        logger.warning("No images found in %s for augmentation", data_dir)
        return

    aug_dir = src_dir / "augmented"
    aug_dir.mkdir(exist_ok=True)

    count = 0
    for img_path in images:
        img = cv2.imread(str(img_path))
        if img is None:
            continue

        base_name = img_path.stem

        for i in range(factor):
            augmented = img.copy()

            # 랜덤 회전 (0, 90, 180, 270)
            angle = np.random.choice([0, 90, 180, 270])
            if angle == 90:
                augmented = cv2.rotate(augmented, cv2.ROTATE_90_CLOCKWISE)
            elif angle == 180:
                augmented = cv2.rotate(augmented, cv2.ROTATE_180)
            elif angle == 270:
                augmented = cv2.rotate(augmented, cv2.ROTATE_90_COUNTERCLOCKWISE)

            # 랜덤 반전
            if np.random.random() > 0.5:
                augmented = cv2.flip(augmented, 1)  # 수평 반전
            if np.random.random() > 0.5:
                augmented = cv2.flip(augmented, 0)  # 수직 반전

            # 밝기/대비 조정
            alpha = np.random.uniform(0.8, 1.2)  # 대비
            beta = np.random.randint(-20, 20)     # 밝기
            augmented = cv2.convertScaleAbs(augmented, alpha=alpha, beta=beta)

            # Gaussian Noise
            if np.random.random() > 0.5:
                noise = np.random.normal(0, 10, augmented.shape).astype(np.float32)
                augmented = np.clip(augmented.astype(np.float32) + noise, 0, 255).astype(np.uint8)

            out_path = aug_dir / f"{base_name}_aug{i:03d}.jpg"
            cv2.imwrite(str(out_path), augmented)
            count += 1

    logger.info("Augmentation complete: %d images generated in %s", count, aug_dir)
