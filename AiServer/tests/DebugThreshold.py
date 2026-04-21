"""DebugThreshold.py — 정상/불량 raw 점수 분포 확인

학습된 모델로 정상과 불량 각각을 추론해서 raw 점수 분포를 보고
image_threshold가 왜 26.37로 계산됐는지 확인한다.
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import cv2
import numpy as np
import torch

from Common.Inferencer import _register_safe_globals
_register_safe_globals()


def main():
    from anomalib.models import Patchcore

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = Patchcore.load_from_checkpoint("models/station1_patchcore.ckpt", map_location=device)
    model.to(device)
    model.eval()

    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std = np.array([0.229, 0.224, 0.225], dtype=np.float32)

    def get_raw_scores(folder: Path) -> list[float]:
        scores = []
        images = []
        for ext in ("*.bmp", "*.png", "*.jpg"):
            images.extend(sorted(folder.glob(ext)))
        for img_path in images:
            img = cv2.imdecode(np.fromfile(str(img_path), dtype=np.uint8), cv2.IMREAD_COLOR)
            if img is None:
                continue
            img224 = cv2.resize(img, (224, 224))
            img224 = cv2.cvtColor(img224, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
            img224 = (img224 - mean) / std
            tensor = torch.from_numpy(img224).permute(2, 0, 1).unsqueeze(0).to(device)

            with torch.no_grad():
                raw_output = model.model(tensor)

            if hasattr(raw_output, "pred_score"):
                scores.append(float(raw_output.pred_score.max().cpu().item()))
        return scores

    print("\n=== 정상 데이터 raw 점수 ===")
    normal_scores = get_raw_scores(Path("data/station1/normal"))
    if normal_scores:
        print(f"  개수:  {len(normal_scores)}")
        print(f"  최소:  {min(normal_scores):.4f}")
        print(f"  최대:  {max(normal_scores):.4f}")
        print(f"  평균:  {sum(normal_scores)/len(normal_scores):.4f}")

    print("\n=== 불량 데이터 raw 점수 ===")
    abnormal_scores = get_raw_scores(Path("data/station1/abnormal"))
    if abnormal_scores:
        print(f"  개수:  {len(abnormal_scores)}")
        print(f"  최소:  {min(abnormal_scores):.4f}")
        print(f"  최대:  {max(abnormal_scores):.4f}")
        print(f"  평균:  {sum(abnormal_scores)/len(abnormal_scores):.4f}")

    print("\n=== Anomalib PostProcessor 현재 threshold ===")
    pp = model.post_processor
    print(f"  image_threshold: {float(pp.image_threshold.cpu().item()):.4f}")
    print(f"  image_min:       {float(pp.image_min.cpu().item()):.4f}")
    print(f"  image_max:       {float(pp.image_max.cpu().item()):.4f}")

    print("\n=== 분석 ===")
    if normal_scores and abnormal_scores:
        n_max = max(normal_scores)
        a_min = min(abnormal_scores)
        print(f"  정상 최대값:     {n_max:.4f}")
        print(f"  불량 최소값:     {a_min:.4f}")
        if a_min > n_max:
            ideal = (n_max + a_min) / 2
            print(f"  [완벽히 구분 가능] 이상적 threshold: {ideal:.4f}")
        else:
            print(f"  [겹침 있음] 경계 모호")


if __name__ == "__main__":
    main()
