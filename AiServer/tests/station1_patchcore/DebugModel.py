"""DebugModel.py — 모델 가중치/메모리뱅크 로드 확인"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

import cv2
import numpy as np
import torch

from Common.Inferencer import _register_safe_globals

_register_safe_globals()


def main():
    from anomalib.models import Patchcore

    model_path = "models/station1_patchcore.ckpt"
    print(f"[로드] {model_path}")

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = Patchcore.load_from_checkpoint(model_path, map_location=device)
    model.to(device)
    model.eval()

    # 내부 PatchcoreModel의 memory_bank 확인
    print(f"\n[model.model 구조]")
    internal = model.model
    print(f"  타입: {type(internal).__name__}")

    # memory_bank 속성 확인
    for attr in dir(internal):
        if "memory" in attr.lower() or "bank" in attr.lower() or "coreset" in attr.lower():
            val = getattr(internal, attr, None)
            if val is not None and not callable(val):
                if torch.is_tensor(val):
                    print(f"  {attr}: Tensor shape={val.shape}, "
                          f"numel={val.numel()}, "
                          f"min={float(val.min()):.4f}, "
                          f"max={float(val.max()):.4f}")
                else:
                    print(f"  {attr}: {type(val).__name__}, value={val}")

    # 두 다른 이미지로 추론 비교
    print(f"\n[다른 두 이미지로 추론 비교]")
    test_dir = Path("data/station1/test")
    imgs = sorted(test_dir.glob("*.bmp"))[:3]
    if len(imgs) < 2:
        imgs = sorted(test_dir.glob("*.png"))[:3]

    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std = np.array([0.229, 0.224, 0.225], dtype=np.float32)

    # PostProcessor의 임계값 확인
    print(f"\n[PostProcessor 상태]")
    if hasattr(model, "post_processor"):
        pp = model.post_processor
        for attr in ["image_threshold", "pixel_threshold",
                     "normalized_image_threshold", "normalized_pixel_threshold",
                     "image_min", "image_max", "pixel_min", "pixel_max"]:
            val = getattr(pp, attr, None)
            if val is not None:
                if torch.is_tensor(val):
                    print(f"  {attr}: {float(val.cpu().item()):.4f}")
                else:
                    print(f"  {attr}: {val}")

    scores = []
    raw_scores = []
    for img_path in imgs:
        img = cv2.imdecode(np.fromfile(str(img_path), dtype=np.uint8), cv2.IMREAD_COLOR)
        img224 = cv2.resize(img, (224, 224))
        img224 = cv2.cvtColor(img224, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        img224 = (img224 - mean) / std
        tensor = torch.from_numpy(img224).permute(2, 0, 1).unsqueeze(0).to(device)

        # raw 출력 (PostProcessor 통과 전)
        with torch.no_grad():
            raw_output = model.model(tensor)
        raw_score = float(raw_output.pred_score.max().cpu().item()) if hasattr(raw_output, "pred_score") else 0.0

        # PostProcessor 통과 후
        with torch.no_grad():
            output = model(tensor)
        score = float(output.pred_score.max().cpu().item()) if hasattr(output, "pred_score") else 0.0

        print(f"  {img_path.name}: raw={raw_score:.4f}, processed={score:.6f}")
        scores.append(score)
        raw_scores.append(raw_score)

    if len(set(f"{s:.4f}" for s in scores)) == 1:
        print(f"\n[!] 모든 이미지가 같은 점수 → 모델 가중치 로드 실패 의심")
    else:
        print(f"\n[OK] 이미지마다 다른 점수 → 모델 정상")


if __name__ == "__main__":
    main()
