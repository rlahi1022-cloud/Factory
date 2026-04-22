"""DebugInference.py — 모델 출력 구조 디버깅용 스크립트

용도: Anomalib 모델이 실제로 반환하는 객체의 구조를 확인한다.
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

import cv2
import numpy as np
import torch

from Common.Config import StationConfig
from Common.Inferencer import Station1Inferencer, _register_safe_globals


def main():
    # 모델 경로
    model_path = "./models/station1_patchcore.ckpt"

    # 테스트 이미지 경로
    test_dir = Path("data/station1/test")
    img_path = next(test_dir.glob("*.png"), None) or next(test_dir.glob("*.jpg"), None)

    if img_path is None:
        print("테스트 이미지 없음")
        return

    print(f"\n[테스트 이미지] {img_path}")

    # 이미지 로드
    img = cv2.imdecode(np.fromfile(str(img_path), dtype=np.uint8), cv2.IMREAD_COLOR)
    print(f"[이미지 shape] {img.shape}")
    print(f"[이미지 dtype] {img.dtype}")

    # 모델 로드
    config = StationConfig(station_id=1, model_path=model_path, device="cpu",
                            anomaly_threshold=0.5, patchcore_input_size=224)
    inferencer = Station1Inferencer(config)
    inferencer.load_model()

    if inferencer._model is None:
        print("[오류] 모델 로드 실패")
        return

    print(f"\n[모델 타입] {type(inferencer._model).__name__}")
    print(f"[모델 클래스] {inferencer._model.__class__.__module__}.{inferencer._model.__class__.__name__}")

    # 전처리
    tensor = inferencer._preprocess(img)
    print(f"\n[입력 텐서 shape] {tensor.shape}")
    print(f"[입력 텐서 min/max] {tensor.min().item():.4f} / {tensor.max().item():.4f}")

    # ── raw 내부 모델 출력 확인 (PostProcessor 우회) ──
    with torch.no_grad():
        if hasattr(inferencer._model, "model") and callable(inferencer._model.model):
            raw_output = inferencer._model.model(tensor)
            print(f"\n[RAW 내부 모델 출력 타입] {type(raw_output).__name__}")
            if hasattr(raw_output, "_fields"):
                for field in raw_output._fields:
                    val = getattr(raw_output, field, None)
                    if val is not None and torch.is_tensor(val):
                        print(f"  raw.{field}: shape={val.shape}, min={val.min():.4f}, max={val.max():.4f}, mean={val.mean():.4f}")
            elif isinstance(raw_output, dict):
                for k, v in raw_output.items():
                    if torch.is_tensor(v):
                        print(f"  raw['{k}']: shape={v.shape}, min={v.min():.4f}, max={v.max():.4f}, mean={v.mean():.4f}")
            elif torch.is_tensor(raw_output):
                print(f"  raw tensor: shape={raw_output.shape}, min={raw_output.min():.4f}, max={raw_output.max():.4f}")
            else:
                print(f"  raw: {raw_output}")

    # ── 전체 Lightning 모델 출력 (PostProcessor 적용됨) ──
    with torch.no_grad():
        output = inferencer._model(tensor)

    # 출력 분석
    print(f"\n[출력 타입] {type(output).__name__}")
    print(f"[출력 전체] {output}")

    # dict/NamedTuple 속성 확인
    if hasattr(output, "_fields"):
        print(f"\n[NamedTuple fields] {output._fields}")
        for field in output._fields:
            val = getattr(output, field, None)
            if val is not None:
                if torch.is_tensor(val):
                    print(f"  {field}: shape={val.shape}, value={val.squeeze()}")
                else:
                    print(f"  {field}: {val}")
    elif isinstance(output, dict):
        print(f"\n[dict keys] {list(output.keys())}")
        for k, v in output.items():
            if torch.is_tensor(v):
                print(f"  {k}: shape={v.shape}, value={v.squeeze()}")
            else:
                print(f"  {k}: {v}")
    elif hasattr(output, "__dict__"):
        print(f"\n[object __dict__] {output.__dict__}")
    else:
        print(f"\n[raw output] {output}")


if __name__ == "__main__":
    main()
