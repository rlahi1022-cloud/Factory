"""Inferencer.py
추론기 구현.
- Station1Inferencer: PatchCore(Anomalib) 기반 입고 검사 (빈 용기 이상탐지)
- Station2Inferencer: YOLO11 + PatchCore 하이브리드 조립 검사
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Optional

import cv2
import numpy as np
import torch

logger = logging.getLogger(__name__)


class BaseInferencer:
    """추론기 베이스. 동기 함수로 정의 — asyncio에서는 to_thread/executor로 호출."""

    def __init__(self, config: Any = None):
        self._config = config

    def load_model(self) -> None:
        raise NotImplementedError

    def infer(self, image: Any) -> dict:
        raise NotImplementedError


# ──────────────────────────────────────────────────────────────
# Station1Inferencer — PatchCore 이상탐지 (입고 검사)
# ──────────────────────────────────────────────────────────────

class Station1Inferencer(BaseInferencer):
    """입고 검사용 PatchCore 추론기.

    Anomalib PatchCore 모델을 로드하여 빈 용기 외관 결함을 탐지한다.
    정상 패턴과 비교하여 이상 점수(anomaly score)가 임계값 초과 시 NG 판정.
    """

    def __init__(self, config: Any = None):
        super().__init__(config)
        self._model = None
        self._transform = None
        self._device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self._threshold: float = getattr(config, "anomaly_threshold", 0.5)
        self._input_size: int = getattr(config, "patchcore_input_size", 224)

    def load_model(self) -> None:
        """Anomalib PatchCore 모델 로드."""
        model_path = getattr(self._config, "model_path", "")
        if not model_path or not Path(model_path).exists():
            logger.warning("Station1 PatchCore model not found: %s — using dummy mode", model_path)
            self._model = None
            return

        try:
            from anomalib.deploy import OpenVINOInferencer as _OV
            # Anomalib >= 1.0: torch 기반 로드
            from anomalib.engine import Engine
            from anomalib.models import Patchcore

            # 체크포인트에서 모델 복원
            self._model = Patchcore.load_from_checkpoint(model_path)
            self._model.to(self._device)
            self._model.eval()
            logger.info("Station1 PatchCore model loaded from %s (device=%s)",
                        model_path, self._device)
        except ImportError:
            logger.warning("anomalib not installed — attempting torch.load fallback")
            try:
                self._model = torch.load(model_path, map_location=self._device, weights_only=False)
                if hasattr(self._model, "eval"):
                    self._model.eval()
                logger.info("Station1 model loaded via torch.load: %s", model_path)
            except Exception as exc:
                logger.error("Station1 model load failed: %s", exc)
                self._model = None
        except Exception as exc:
            logger.error("Station1 PatchCore load error: %s", exc)
            self._model = None

    def infer(self, image: Any) -> dict:
        """PatchCore 추론.

        Args:
            image: numpy.ndarray (BGR) 또는 None (더미 모드).
        Returns:
            {result, score, defect, heatmap}
        """
        if image is None or self._model is None:
            return {"result": "OK", "score": 0.0, "defect": "", "heatmap": None}

        try:
            input_tensor = self._preprocess(image)

            with torch.no_grad():
                output = self._model(input_tensor)

            # Anomalib 출력 파싱
            if isinstance(output, dict):
                anomaly_score = float(output.get("pred_scores", torch.tensor(0.0)).cpu().item()
                                      if torch.is_tensor(output.get("pred_scores"))
                                      else output.get("pred_scores", 0.0))
                anomaly_map = output.get("anomaly_maps", None)
            elif isinstance(output, (tuple, list)):
                anomaly_score = float(output[0].cpu().item() if torch.is_tensor(output[0]) else output[0])
                anomaly_map = output[1] if len(output) > 1 else None
            else:
                anomaly_score = float(output.cpu().item() if torch.is_tensor(output) else output)
                anomaly_map = None

            # 히트맵 변환
            heatmap = None
            if anomaly_map is not None:
                if torch.is_tensor(anomaly_map):
                    heatmap = anomaly_map.squeeze().cpu().numpy()
                else:
                    heatmap = np.array(anomaly_map).squeeze()
                heatmap = cv2.resize(heatmap, (image.shape[1], image.shape[0]))
                heatmap = (heatmap * 255).clip(0, 255).astype(np.uint8)
                heatmap = cv2.applyColorMap(heatmap, cv2.COLORMAP_JET)

            is_ng = anomaly_score > self._threshold
            defect_type = self._classify_defect(anomaly_score) if is_ng else ""

            return {
                "result": "NG" if is_ng else "OK",
                "score": round(anomaly_score, 4),
                "defect": defect_type,
                "heatmap": heatmap,
            }
        except Exception as exc:
            logger.exception("Station1 infer error: %s", exc)
            return {"result": "OK", "score": 0.0, "defect": "", "heatmap": None}

    def _preprocess(self, image: np.ndarray) -> torch.Tensor:
        """BGR 이미지를 PatchCore 입력 텐서로 변환."""
        img = cv2.resize(image, (self._input_size, self._input_size))
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = img.astype(np.float32) / 255.0
        # ImageNet 정규화
        mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
        std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
        img = (img - mean) / std
        tensor = torch.from_numpy(img).permute(2, 0, 1).unsqueeze(0)
        return tensor.to(self._device)

    @staticmethod
    def _classify_defect(score: float) -> str:
        """이상 점수 범위에 따른 결함 유형 분류."""
        if score > 0.8:
            return "crack"
        elif score > 0.65:
            return "contamination"
        elif score > 0.5:
            return "scratch"
        return "minor_defect"


# ──────────────────────────────────────────────────────────────
# Station2Inferencer — YOLO11 + PatchCore 하이브리드 (조립 검사)
# ──────────────────────────────────────────────────────────────

class Station2Inferencer(BaseInferencer):
    """조립 검사용 YOLO11 + PatchCore 하이브리드 추론기.

    1차: YOLO11로 cap/label/liquid_level 존재 여부 + IoU 기반 위치 판정
    2차: PatchCore로 라벨 표면 이상 점수 판정
    → 두 판정 중 하나라도 NG이면 최종 NG
    """

    # YOLO11 클래스 이름 매핑
    CLASS_NAMES = {0: "cap", 1: "label", 2: "liquid_level"}
    REQUIRED_CLASSES = {"cap", "label", "liquid_level"}

    def __init__(self, config: Any = None):
        super().__init__(config)
        self._yolo_model = None
        self._patchcore_model = None
        self._device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

        self._yolo_conf = getattr(config, "yolo_conf_threshold", 0.5)
        self._yolo_iou = getattr(config, "yolo_iou_threshold", 0.45)
        self._yolo_input_size = getattr(config, "yolo_input_size", 640)
        self._patchcore_input_size = getattr(config, "patchcore_input_size", 224)
        self._anomaly_threshold = getattr(config, "anomaly_threshold", 0.5)

        # 각 요소의 정상 위치 기준 (이미지 비율 기준, 초기값은 학습 데이터에서 결정)
        self._reference_boxes = {
            "cap":          {"y_min": 0.0,  "y_max": 0.15, "x_min": 0.25, "x_max": 0.75},
            "label":        {"y_min": 0.20, "y_max": 0.75, "x_min": 0.10, "x_max": 0.90},
            "liquid_level": {"y_min": 0.60, "y_max": 0.90, "x_min": 0.15, "x_max": 0.85},
        }

    def load_model(self) -> None:
        """YOLO11 + PatchCore 모델 로드."""
        # YOLO11 로드
        yolo_path = getattr(self._config, "model_path", "")
        if yolo_path and Path(yolo_path).exists():
            try:
                from ultralytics import YOLO
                self._yolo_model = YOLO(yolo_path)
                logger.info("Station2 YOLO11 model loaded: %s", yolo_path)
            except Exception as exc:
                logger.error("Station2 YOLO11 load failed: %s", exc)
                self._yolo_model = None
        else:
            logger.warning("Station2 YOLO11 model not found: %s — using dummy mode", yolo_path)

        # PatchCore 로드 (라벨 표면 품질)
        pc_path = getattr(self._config, "patchcore_model_path", "")
        if pc_path and Path(pc_path).exists():
            try:
                from anomalib.models import Patchcore
                self._patchcore_model = Patchcore.load_from_checkpoint(pc_path)
                self._patchcore_model.to(self._device)
                self._patchcore_model.eval()
                logger.info("Station2 PatchCore model loaded: %s", pc_path)
            except ImportError:
                try:
                    self._patchcore_model = torch.load(pc_path, map_location=self._device, weights_only=False)
                    if hasattr(self._patchcore_model, "eval"):
                        self._patchcore_model.eval()
                    logger.info("Station2 PatchCore loaded via torch.load: %s", pc_path)
                except Exception as exc:
                    logger.error("Station2 PatchCore load failed: %s", exc)
                    self._patchcore_model = None
            except Exception as exc:
                logger.error("Station2 PatchCore load error: %s", exc)
                self._patchcore_model = None
        else:
            logger.warning("Station2 PatchCore model not found: %s — surface check disabled", pc_path)

    def infer(self, image: Any) -> dict:
        """YOLO11 + PatchCore 하이브리드 추론.

        Returns:
            {result, score, defects[], detections[], patchcore_score, bbox_overlay}
        """
        if image is None:
            return self._default_result()

        defects: list[str] = []
        detections: list[dict] = []
        patchcore_score = 0.0
        bbox_overlay = image.copy()

        # ── 1차: YOLO11 객체탐지 ──
        yolo_ok, yolo_detections, bbox_overlay = self._run_yolo(image, bbox_overlay)
        detections = yolo_detections

        if not yolo_ok:
            # 누락된 요소 파악
            detected_classes = {d["class"] for d in detections}
            missing = self.REQUIRED_CLASSES - detected_classes
            for m in missing:
                defects.append(f"{m}_missing")

            # 위치 이탈 검사
            for det in detections:
                if not det.get("position_ok", True):
                    defects.append(f"{det['class']}_misaligned")

        # ── 2차: PatchCore 라벨 표면 이상탐지 ──
        label_roi = self._extract_label_roi(image, detections)
        if label_roi is not None:
            patchcore_score = self._run_patchcore(label_roi)
            if patchcore_score > self._anomaly_threshold:
                defects.append("label_surface_defect")

        # ── 최종 판정 ──
        is_ng = len(defects) > 0
        total_score = max(patchcore_score, max((d.get("conf", 0) for d in detections), default=0))

        # 캡/라벨/충전량 개별 판정
        detected_classes = {d["class"] for d in detections}
        cap_ok = "cap" in detected_classes and "cap_missing" not in defects and "cap_misaligned" not in defects
        label_ok = "label" in detected_classes and "label_missing" not in defects and "label_misaligned" not in defects
        fill_ok = "liquid_level" in detected_classes and "liquid_level_missing" not in defects

        return {
            "result": "NG" if is_ng else "OK",
            "score": round(total_score, 4),
            "defect": defects[0] if defects else "",
            "defects": defects,
            "detections": detections,
            "patchcore_score": round(patchcore_score, 4),
            "cap_ok": cap_ok,
            "label_ok": label_ok,
            "fill_ok": fill_ok,
            "yolo_detections": detections,
            "bbox_overlay": bbox_overlay,
        }

    # ── YOLO11 추론 ──

    def _run_yolo(self, image: np.ndarray, overlay: np.ndarray) -> tuple[bool, list[dict], np.ndarray]:
        """YOLO11 추론 수행."""
        if self._yolo_model is None:
            return True, [], overlay

        try:
            results = self._yolo_model.predict(
                source=image,
                conf=self._yolo_conf,
                iou=self._yolo_iou,
                imgsz=self._yolo_input_size,
                verbose=False,
            )

            if not results or len(results) == 0:
                return False, [], overlay

            result = results[0]
            detections: list[dict] = []
            all_ok = True
            h, w = image.shape[:2]

            for box in result.boxes:
                cls_id = int(box.cls.cpu().item())
                conf = float(box.conf.cpu().item())
                x1, y1, x2, y2 = box.xyxy[0].cpu().numpy().astype(int).tolist()
                cls_name = self.CLASS_NAMES.get(cls_id, f"class_{cls_id}")

                # IoU 기반 위치 판정
                position_ok = self._check_position(cls_name, x1, y1, x2, y2, w, h)
                if not position_ok:
                    all_ok = False

                detections.append({
                    "class": cls_name,
                    "bbox": [x1, y1, x2, y2],
                    "conf": round(conf, 3),
                    "position_ok": position_ok,
                })

                # 바운딩 박스 오버레이
                color = (0, 255, 0) if position_ok else (0, 0, 255)
                cv2.rectangle(overlay, (x1, y1), (x2, y2), color, 2)
                label_text = f"{cls_name} {conf:.2f}"
                cv2.putText(overlay, label_text, (x1, y1 - 5),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

            # 필수 클래스 누락 확인
            detected_classes = {d["class"] for d in detections}
            if not self.REQUIRED_CLASSES.issubset(detected_classes):
                all_ok = False

            return all_ok, detections, overlay

        except Exception as exc:
            logger.exception("YOLO11 inference error: %s", exc)
            return True, [], overlay

    def _check_position(self, cls_name: str, x1: int, y1: int, x2: int, y2: int,
                        img_w: int, img_h: int) -> bool:
        """탐지된 객체 위치가 기준 범위 안에 있는지 IoU 비교."""
        ref = self._reference_boxes.get(cls_name)
        if ref is None:
            return True

        # 정규화 좌표로 변환
        det_box = (x1 / img_w, y1 / img_h, x2 / img_w, y2 / img_h)
        ref_box = (ref["x_min"], ref["y_min"], ref["x_max"], ref["y_max"])

        iou = self._compute_iou(det_box, ref_box)
        return iou > 0.3  # IoU 30% 이상이면 정상 위치

    @staticmethod
    def _compute_iou(box_a: tuple, box_b: tuple) -> float:
        """두 박스의 IoU 계산 (정규화 좌표)."""
        xa = max(box_a[0], box_b[0])
        ya = max(box_a[1], box_b[1])
        xb = min(box_a[2], box_b[2])
        yb = min(box_a[3], box_b[3])

        inter = max(0, xb - xa) * max(0, yb - ya)
        area_a = (box_a[2] - box_a[0]) * (box_a[3] - box_a[1])
        area_b = (box_b[2] - box_b[0]) * (box_b[3] - box_b[1])
        union = area_a + area_b - inter

        return inter / union if union > 0 else 0.0

    # ── PatchCore 라벨 표면 추론 ──

    def _extract_label_roi(self, image: np.ndarray, detections: list[dict]) -> Optional[np.ndarray]:
        """탐지된 라벨 영역을 crop."""
        for det in detections:
            if det["class"] == "label":
                x1, y1, x2, y2 = det["bbox"]
                h, w = image.shape[:2]
                x1 = max(0, x1)
                y1 = max(0, y1)
                x2 = min(w, x2)
                y2 = min(h, y2)
                if x2 > x1 and y2 > y1:
                    return image[y1:y2, x1:x2].copy()
        return None

    def _run_patchcore(self, label_roi: np.ndarray) -> float:
        """라벨 ROI에 대해 PatchCore 이상 점수 산출."""
        if self._patchcore_model is None:
            return 0.0

        try:
            img = cv2.resize(label_roi, (self._patchcore_input_size, self._patchcore_input_size))
            img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            img = img.astype(np.float32) / 255.0
            mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
            std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
            img = (img - mean) / std
            tensor = torch.from_numpy(img).permute(2, 0, 1).unsqueeze(0).to(self._device)

            with torch.no_grad():
                output = self._patchcore_model(tensor)

            if isinstance(output, dict):
                score = output.get("pred_scores", torch.tensor(0.0))
                return float(score.cpu().item() if torch.is_tensor(score) else score)
            elif isinstance(output, (tuple, list)):
                return float(output[0].cpu().item() if torch.is_tensor(output[0]) else output[0])
            else:
                return float(output.cpu().item() if torch.is_tensor(output) else output)
        except Exception as exc:
            logger.exception("PatchCore label surface infer error: %s", exc)
            return 0.0

    @staticmethod
    def _default_result() -> dict:
        return {
            "result": "OK",
            "score": 0.0,
            "defect": "",
            "defects": [],
            "detections": [],
            "patchcore_score": 0.0,
            "cap_ok": True,
            "label_ok": True,
            "fill_ok": True,
            "yolo_detections": [],
            "bbox_overlay": None,
        }
