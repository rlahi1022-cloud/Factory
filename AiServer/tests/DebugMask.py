"""DebugMask.py — pred_mask 실제 값 확인

Anomalib이 실제로 어떤 pred_mask를 반환하는지 확인하는 디버그 스크립트.
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import cv2
import numpy as np
import torch

from Common.Config import StationConfig
from Common.Inferencer import Station1Inferencer


def main():
    model_path = "./models/station1_patchcore.ckpt"
    test_dir = Path("data/station1/test")

    # 파이리 이미지(강한 결함) 찾기
    images = sorted(test_dir.glob("*.png"))
    if not images:
        print("테스트 이미지 없음")
        return

    # 30번 이후 불량으로 추정되는 이미지 중 하나 선택
    img_path = images[30] if len(images) > 30 else images[0]
    print(f"\n[테스트 이미지] {img_path}")

    img = cv2.imdecode(np.fromfile(str(img_path), dtype=np.uint8), cv2.IMREAD_COLOR)
    print(f"[이미지 shape] {img.shape}")

    config = StationConfig(station_id=1, model_path=model_path, device="cpu",
                           anomaly_threshold=0.5, patchcore_input_size=224)
    inferencer = Station1Inferencer(config)
    inferencer.load_model()

    if inferencer._model is None:
        print("[오류] 모델 로드 실패")
        return

    # 전처리
    tensor = inferencer._preprocess(img)

    # raw 모델 출력
    with torch.no_grad():
        raw_output = inferencer._model.model(tensor)

    print(f"\n[raw output 필드] {raw_output._fields if hasattr(raw_output, '_fields') else type(raw_output)}")
    if hasattr(raw_output, 'pred_score'):
        print(f"  raw.pred_score: {float(raw_output.pred_score.max())}")
    if hasattr(raw_output, 'anomaly_map'):
        am = raw_output.anomaly_map
        print(f"  raw.anomaly_map: shape={am.shape}, min={float(am.min()):.4f}, max={float(am.max()):.4f}")
    if hasattr(raw_output, 'pred_mask'):
        pm = raw_output.pred_mask
        if pm is None:
            print(f"  raw.pred_mask: None (PostProcessor에서 계산 필요)")
        else:
            print(f"  raw.pred_mask: shape={pm.shape}, True_count={int(pm.sum())}/{pm.numel()}")

    # PostProcessor 직접 호출
    print(f"\n[모델에 post_processor 있는가?] {hasattr(inferencer._model, 'post_processor')}")
    if hasattr(inferencer._model, "post_processor") and inferencer._model.post_processor is not None:
        pp = inferencer._model.post_processor
        print(f"[PostProcessor 타입] {type(pp).__name__}")
        # 속성 확인
        print(f"[PostProcessor 속성]")
        for attr in dir(pp):
            if not attr.startswith("_") and "threshold" in attr.lower():
                try:
                    val = getattr(pp, attr)
                    if torch.is_tensor(val):
                        print(f"  {attr}: {float(val.cpu().item())}")
                    elif not callable(val):
                        print(f"  {attr}: {val}")
                except Exception:
                    pass

        # PostProcessor 호출
        try:
            with torch.no_grad():
                processed = pp(raw_output)
            print(f"\n[processed 타입] {type(processed).__name__}")
            if hasattr(processed, '_fields'):
                for field in processed._fields:
                    val = getattr(processed, field, None)
                    if val is not None and torch.is_tensor(val):
                        if val.dtype == torch.bool or 'mask' in field.lower():
                            print(f"  {field}: shape={val.shape}, True={int(val.sum())}/{val.numel()}")
                        else:
                            print(f"  {field}: shape={val.shape}, min={float(val.min()):.4f}, max={float(val.max()):.4f}")
        except Exception as exc:
            print(f"[PostProcessor 호출 실패] {exc}")

    # 최종 infer 결과
    print(f"\n[infer() 결과]")
    result = inferencer.infer(img)
    print(f"  result: {result.get('result')}")
    print(f"  score: {result.get('score')}")
    print(f"  pixel_threshold: {result.get('pixel_threshold')}")
    pm = result.get('pred_mask')
    if pm is not None:
        print(f"  pred_mask: shape={pm.shape}, dtype={pm.dtype}, white={int((pm > 0).sum())}/{pm.size}")
        # 저장해서 눈으로 확인
        cv2.imwrite("debug_mask.png", pm)
        print(f"  → debug_mask.png 저장")
    else:
        print(f"  pred_mask: None")


if __name__ == "__main__":
    main()
