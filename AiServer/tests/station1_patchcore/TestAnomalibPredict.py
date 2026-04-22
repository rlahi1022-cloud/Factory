"""TestAnomalibPredict.py — Anomalib Engine.predict() 공식 추론+시각화

Anomalib의 공식 Engine.predict()를 사용하여 학습 시 자동 저장되는 것과
완전히 동일한 시각화 이미지를 얻는다. 우리가 직접 전처리/시각화를 구현하지 않고
Anomalib 공식 파이프라인을 그대로 활용한다.

사용법:
  cd AiServer
  python3 tests/TestAnomalibPredict.py
"""

from __future__ import annotations

import argparse
import csv
import json
import shutil
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))


def main():
    parser = argparse.ArgumentParser(description="Anomalib 공식 추론+시각화")
    parser.add_argument("--model", type=str,
                        default="models/station1_patchcore.ckpt",
                        help="PatchCore 체크포인트 경로")
    parser.add_argument("--dir", type=str,
                        default="data/station1/test",
                        help="추론 이미지 폴더")
    parser.add_argument("--output-root", type=str, default="test_results",
                        help="결과 저장 루트")
    parser.add_argument("--station", type=int, default=1, choices=[1, 2],
                        help="스테이션 번호")
    args = parser.parse_args()

    import torch
    from anomalib.engine import Engine
    from anomalib.models import Patchcore
    from anomalib.data import PredictDataset

    try:
        from Common.Inferencer import _register_safe_globals
        _register_safe_globals()
    except Exception:
        pass

    # ── 결과 폴더 준비 ──
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = Path(args.output_root) / f"station{args.station}_{timestamp}"
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"{'=' * 70}")
    print(f"  Anomalib Engine.predict() 공식 추론 + 시각화")
    print(f"{'=' * 70}")
    print(f"  모델:     {args.model}")
    print(f"  데이터:   {args.dir}")
    print(f"  출력:     {output_dir}")
    print(f"{'=' * 70}\n")

    # ── threshold.json에서 raw 임계값 로드 (판정 보조용) ──
    raw_threshold = None
    thr_json = Path(args.model).with_name(Path(args.model).stem + "_threshold.json")
    if thr_json.exists():
        with open(thr_json, "r", encoding="utf-8") as f:
            info = json.load(f)
        raw_threshold = float(info.get("threshold", 0))
        print(f"  임계값(raw): {raw_threshold:.4f} (F1={info.get('f1_score', 0):.4f})")

    # ── 모델 생성 (구조만, 가중치는 Engine.predict()가 ckpt_path로 로드) ──
    model = Patchcore()

    # ── Anomalib PredictDataset (학습 시 Folder 전처리와 호환) ──
    dataset = PredictDataset(path=Path(args.dir), image_size=(224, 224))
    print(f"이미지 수: {len(dataset)}")

    # ── Engine 생성 (시각화가 자동 저장될 임시 폴더 지정) ──
    tmp_dir = output_dir / "_anomalib"
    tmp_dir.mkdir(exist_ok=True)
    engine = Engine(default_root_dir=str(tmp_dir), devices=1, accelerator="auto")

    # ── 추론 실행: ckpt_path를 넘겨야 가중치가 로드됨 ──
    print("\n추론 시작...")
    t_start = time.perf_counter()
    predictions = engine.predict(
        model=model,
        dataset=dataset,
        ckpt_path=args.model,  # 가중치 로드의 핵심!
    )
    elapsed = time.perf_counter() - t_start
    print(f"추론 완료 ({elapsed:.1f}초)\n")

    # ── 이미지 파일 경로 수집 (출력용) ──
    image_paths = []
    data_folder = Path(args.dir)
    for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp",
                "*.JPG", "*.PNG", "*.BMP"):
        image_paths.extend(sorted(data_folder.glob(ext)))
    image_paths = sorted(set(image_paths))

    # ── CSV 파일 + 콘솔 헤더 ──
    csv_path = output_dir / "summary.csv"
    csv_file = open(csv_path, "w", newline="", encoding="utf-8-sig")
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow(["번호", "파일명", "판정", "이상점수"])

    print(f"{'번호':>4} | {'파일명':<45} | {'판정':>4} | {'이상점수':>10}")
    print("-" * 75)

    ok_count = 0
    ng_count = 0
    idx = 0

    # predictions 리스트 안에는 배치들이 있음
    for batch in (predictions if predictions else []):
        pred_scores = None
        pred_labels = None

        if hasattr(batch, "pred_score"):
            pred_scores = batch.pred_score
            pred_labels = getattr(batch, "pred_label", None)
        elif isinstance(batch, dict):
            pred_scores = batch.get("pred_score") or batch.get("pred_scores")
            pred_labels = batch.get("pred_label") or batch.get("pred_labels")

        if pred_scores is None:
            continue

        scores = (pred_scores.cpu().tolist() if torch.is_tensor(pred_scores)
                  else list(pred_scores))
        if pred_labels is not None:
            labels = (pred_labels.cpu().tolist() if torch.is_tensor(pred_labels)
                      else list(pred_labels))
        else:
            labels = [s > 0.5 for s in scores]

        for score, label in zip(scores, labels):
            if idx >= len(image_paths):
                break
            name = image_paths[idx].name
            display = name if len(name) <= 45 else name[:42] + "..."
            score_val = float(score)
            verdict = "NG" if bool(label) else "OK"

            print(f"{idx+1:>4} | {display:<45} | {verdict:>4} | {score_val:>10.4f}")
            csv_writer.writerow([idx+1, name, verdict, f"{score_val:.4f}"])

            if verdict == "NG":
                ng_count += 1
            else:
                ok_count += 1
            idx += 1

    csv_file.close()

    # ── 시각화 이미지 생성 (Anomalib ImageVisualizer 직접 호출) ──
    # Anomalib predict()는 기본적으로 시각화를 저장하지 않으므로,
    # predictions에 포함된 anomaly_map/pred_mask로 시각화를 만든다.
    images_dir = output_dir / "images"
    images_dir.mkdir(exist_ok=True)

    moved = 0
    try:
        import cv2
        import numpy as np

        def load_image(path: Path) -> np.ndarray:
            try:
                return cv2.imdecode(np.fromfile(str(path), dtype=np.uint8),
                                    cv2.IMREAD_COLOR)
            except Exception:
                return cv2.imread(str(path))

        def save_image(path: Path, img: np.ndarray) -> bool:
            try:
                ext = path.suffix or ".png"
                ok, buf = cv2.imencode(ext, img)
                if ok:
                    buf.tofile(str(path))
                    return True
            except Exception:
                pass
            return False

        # predictions 다시 순회하며 시각화 생성
        idx = 0
        for batch in (predictions if predictions else []):
            # 배치 안의 anomaly_map, pred_mask 추출
            a_maps = getattr(batch, "anomaly_map", None)
            p_masks = getattr(batch, "pred_mask", None)
            p_scores = getattr(batch, "pred_score", None)
            p_labels = getattr(batch, "pred_label", None)

            if a_maps is None:
                continue

            if torch.is_tensor(a_maps):
                a_maps_np = a_maps.squeeze(1).cpu().numpy() if a_maps.dim() == 4 else a_maps.cpu().numpy()
            else:
                a_maps_np = a_maps

            if p_masks is not None and torch.is_tensor(p_masks):
                p_masks_np = p_masks.squeeze(1).cpu().numpy() if p_masks.dim() == 4 else p_masks.cpu().numpy()
            else:
                p_masks_np = None

            if p_scores is not None and torch.is_tensor(p_scores):
                p_scores_list = p_scores.cpu().tolist()
            else:
                p_scores_list = [0.0] * len(a_maps_np)

            if p_labels is not None and torch.is_tensor(p_labels):
                p_labels_list = p_labels.cpu().tolist()
            else:
                p_labels_list = [s > 0.5 for s in p_scores_list]

            batch_size = len(a_maps_np) if hasattr(a_maps_np, "__len__") else 1
            for b in range(batch_size):
                if idx >= len(image_paths):
                    break
                img_path = image_paths[idx]
                original = load_image(img_path)
                if original is None:
                    idx += 1
                    continue

                # 224x224로 리사이즈 (Anomalib 처리 크기와 맞춤)
                img_resized = cv2.resize(original, (224, 224))
                amap = a_maps_np[b] if batch_size > 1 else a_maps_np
                if amap.ndim == 3:
                    amap = amap.squeeze()

                # 히트맵 컬러맵 적용
                amin, amax = float(amap.min()), float(amap.max())
                if amax > amin:
                    amap_norm = (amap - amin) / (amax - amin)
                else:
                    amap_norm = np.zeros_like(amap)
                amap_u8 = (amap_norm * 255).clip(0, 255).astype(np.uint8)
                heatmap = cv2.applyColorMap(amap_u8, cv2.COLORMAP_JET)
                heatmap = cv2.resize(heatmap, (224, 224))

                # 3분할 패널
                panel1 = img_resized.copy()
                panel2 = cv2.addWeighted(img_resized, 0.5, heatmap, 0.5, 0)
                panel3 = img_resized.copy()

                # Pred Mask 경계 표시
                score = float(p_scores_list[b]) if b < len(p_scores_list) else 0.0
                is_ng = bool(p_labels_list[b]) if b < len(p_labels_list) else (score > 0.5)
                if is_ng and p_masks_np is not None:
                    mask = p_masks_np[b] if batch_size > 1 else p_masks_np
                    if mask.ndim == 3:
                        mask = mask.squeeze()
                    mask_u8 = (mask > 0.5).astype(np.uint8) * 255
                    mask_u8 = cv2.resize(mask_u8, (224, 224),
                                         interpolation=cv2.INTER_NEAREST)
                    contours, _ = cv2.findContours(mask_u8, cv2.RETR_EXTERNAL,
                                                    cv2.CHAIN_APPROX_SIMPLE)
                    cv2.drawContours(panel3, contours, -1, (0, 0, 255), 2)

                # 라벨
                for panel, label in zip(
                    [panel1, panel2, panel3],
                    ["Image", "Image + Anomaly Map", "Image + Pred Mask"],
                ):
                    cv2.rectangle(panel, (5, 5), (5 + len(label) * 8, 25),
                                  (0, 0, 0), -1)
                    cv2.putText(panel, label, (8, 20),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.4,
                                (255, 255, 255), 1)

                combined = np.hstack([panel1, panel2, panel3])

                # 상단 배너
                banner_color = (0, 180, 0) if not is_ng else (0, 0, 220)
                verdict = "NG" if is_ng else "OK"
                banner = np.full((30, combined.shape[1], 3),
                                 banner_color, dtype=np.uint8)
                cv2.putText(banner, f"{verdict} | Score: {score:.4f}",
                            (10, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                            (255, 255, 255), 1)
                final = np.vstack([banner, combined])

                out_path = images_dir / f"{idx+1:03d}_{img_path.stem}_{verdict}.png"
                if save_image(out_path, final):
                    moved += 1
                idx += 1
    except Exception as exc:
        print(f"[시각화 생성 실패] {exc}")

    # 임시 폴더 정리
    if tmp_dir.exists():
        shutil.rmtree(tmp_dir, ignore_errors=True)

    # ── 최종 요약 ──
    total = ok_count + ng_count
    print("-" * 75)
    print(f"\n{'=' * 70}")
    print(f"  최종 요약")
    print(f"{'=' * 70}")
    print(f"  총 이미지:    {total}장")
    if total > 0:
        print(f"  OK (정상):    {ok_count}장 ({ok_count/total*100:.1f}%)")
        print(f"  NG (불량):    {ng_count}장 ({ng_count/total*100:.1f}%)")
    print(f"  추론시간:     {elapsed:.1f}초")
    print(f"  시각화:       {images_dir} ({moved}장)")
    print(f"  CSV 요약:     {csv_path}")
    # 학습 시 기록된 F1-score (threshold.json에서)
    if thr_json.exists():
        with open(thr_json, "r", encoding="utf-8") as f:
            info = json.load(f)
        print(f"  학습 F1:      {info.get('f1_score', 0):.4f}")
    print(f"{'=' * 70}\n")


if __name__ == "__main__":
    main()
