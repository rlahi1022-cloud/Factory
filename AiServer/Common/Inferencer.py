"""Inferencer.py
추론기 인터페이스 골격.
실제 PatchCore / YOLO11 모델은 추후 주입. 본 파일에서는 호출 규약만 정의.
"""

from __future__ import annotations

from typing import Any


class BaseInferencer:
    """추론기 베이스. 동기 함수로 정의 — asyncio에서는 to_thread/executor로 호출."""

    def load_model(self) -> None:
        """모델 로드. 본 골격에서는 no-op."""
        pass

    def infer(self, image: Any) -> dict:
        """이미지 추론.

        Args:
            image: numpy.ndarray (BGR) 가정.
        Returns:
            결과 dict — 최소 키: result(OK|NG), score, defect.
        """
        raise NotImplementedError


class Station1Inferencer(BaseInferencer):
    """입고 검사용 — PatchCore 자리."""

    def infer(self, image: Any) -> dict:
        # TODO: PatchCore 추론 결과 매핑
        # 골격: 항상 OK 반환
        return {
            "result": "OK",
            "score": 0.0,
            "defect": "",
        }


class Station2Inferencer(BaseInferencer):
    """조립 검사용 — YOLO11 + PatchCore 하이브리드 자리."""

    def infer(self, image: Any) -> dict:
        # TODO: YOLO11 + PatchCore 융합 결과 매핑
        return {
            "result": "OK",
            "score": 0.0,
            "defect": "",
            "detections": [],
        }
