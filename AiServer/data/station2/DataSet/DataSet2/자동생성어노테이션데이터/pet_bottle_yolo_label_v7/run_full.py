"""
Run auto-labeling over the full dataset and produce a YOLO11-ready package.

Output layout (in /home/claude/yolo_dataset/):
  images/
    train/     <- 80% of images (flat, .bmp)
    val/       <- 20% of images
  labels/
    train/     <- matching .txt files
    val/
  previews/    <- visualization with boxes drawn (for review)
    ...
  data.yaml    <- YOLO dataset config
  review.csv   <- per-image detection summary for triage
  README.md
"""
import os
import csv
import random
import shutil
from pathlib import Path

import cv2
from auto_label import (label_image, draw_preview, to_yolo, CLASSES)

SRC = Path("img_data")
OUT = Path("yolo_dataset")

# Map folder -> expected objects
# Normal: has cap, has label, has fill
# 캡없음: no cap, has label, has fill
# 캡미세하게열림: has cap, has label, has fill
# 라벨없음: has cap, no label, has fill
# 라벨각도다름: has cap, has label, has fill
# 충전량: has cap, has label, has fill (just different amount)
FOLDER_EXPECTATIONS = {
    "2_normal":                 {"has_cap": True,  "has_label": True,  "tag": "normal"},
    "2_abnormal/캡없음":           {"has_cap": False, "has_label": True,  "tag": "abn_no_cap"},
    "2_abnormal/캡미세하게열림":      {"has_cap": True,  "has_label": True,  "tag": "abn_cap_open"},
    "2_abnormal/라벨없음":          {"has_cap": True,  "has_label": False, "tag": "abn_no_label"},
    "2_abnormal/라벨각도다름":        {"has_cap": True,  "has_label": True,  "tag": "abn_label_angle"},
    "2_abnormal/충전량":            {"has_cap": True,  "has_label": True,  "tag": "abn_underfill"},
}


def collect_images():
    """Return list of (src_path, expect_dict, category_tag)."""
    items = []
    for rel, expect in FOLDER_EXPECTATIONS.items():
        folder = SRC / rel
        if not folder.exists():
            print(f"WARN: {folder} does not exist")
            continue
        for f in sorted(folder.iterdir()):
            if f.suffix.lower() in {".bmp", ".png", ".jpg", ".jpeg"}:
                tag = expect["tag"]  # use ASCII tag, not Korean folder name
                items.append((f, expect, tag))
    return items


def setup_output_dirs():
    if OUT.exists():
        shutil.rmtree(OUT)
    for sub in ("images/train", "images/val",
                "labels/train", "labels/val",
                "previews"):
        (OUT / sub).mkdir(parents=True, exist_ok=True)


def main():
    random.seed(42)
    items = collect_images()
    print(f"Collected {len(items)} images")
    setup_output_dirs()

    # 80/20 train/val split, stratified by category
    by_tag = {}
    for item in items:
        by_tag.setdefault(item[2], []).append(item)

    train_items = []
    val_items = []
    for tag, lst in by_tag.items():
        random.shuffle(lst)
        n = len(lst)
        n_val = max(1, n // 5)
        val_items.extend(lst[:n_val])
        train_items.extend(lst[n_val:])
    print(f"train={len(train_items)}, val={len(val_items)}")

    # Process every image
    review_rows = []
    for split, lst in [("train", train_items), ("val", val_items)]:
        for idx, (src, expect, tag) in enumerate(lst):
            # Copy image
            stem = f"{tag}__{src.stem}"
            dst_img = OUT / f"images/{split}/{stem}.bmp"
            shutil.copy(src, dst_img)

            # Run detector
            gray, results = label_image(src, expect=expect)
            if gray is None:
                print(f"ERROR reading {src}")
                continue
            h, w = gray.shape

            # Save YOLO label file
            label_path = OUT / f"labels/{split}/{stem}.txt"
            with open(label_path, "w") as fout:
                for cid, bbox in results:
                    cx, cy, bw, bh = to_yolo(bbox, w, h)
                    # Clip to [0,1]
                    cx = min(max(cx, 0.0), 1.0)
                    cy = min(max(cy, 0.0), 1.0)
                    bw = min(max(bw, 0.0), 1.0)
                    bh = min(max(bh, 0.0), 1.0)
                    fout.write(f"{cid} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}\n")

            # Save preview (downsized for faster browsing)
            preview_path = OUT / f"previews/{stem}.jpg"
            # Downsize the preview a bit for a smaller package
            h_small = 720
            scale = h_small / h
            small = cv2.resize(gray, (int(w * scale), h_small),
                               interpolation=cv2.INTER_AREA)
            # Scale bboxes
            small_results = [
                (cid, tuple(int(v * scale) for v in bbox))
                for cid, bbox in results
            ]
            draw_preview(small, small_results, preview_path)

            # Triage: count detections
            has = {CLASSES[i]: False for i in range(3)}
            for cid, _ in results:
                has[CLASSES[cid]] = True
            # Expected flags
            expected_cap = expect.get("has_cap", True)
            expected_label = expect.get("has_label", True)
            # Simple quality flag: needs review if expected class missing
            flags = []
            if expected_cap and not has["cap"]:
                flags.append("MISSING_CAP")
            if (not expected_cap) and has["cap"]:
                flags.append("UNEXPECTED_CAP")
            if expected_label and not has["label"]:
                flags.append("MISSING_LABEL")
            if (not expected_label) and has["label"]:
                flags.append("UNEXPECTED_LABEL")
            if not has["fill_level"]:
                flags.append("MISSING_FILL")
            review_rows.append({
                "split": split,
                "category": tag,
                "filename": dst_img.name,
                "cap": int(has["cap"]),
                "label": int(has["label"]),
                "fill_level": int(has["fill_level"]),
                "num_detections": len(results),
                "flags": "|".join(flags),
            })
            if (len(review_rows) % 20) == 0:
                print(f"  processed {len(review_rows)}")

    # Write review CSV
    with open(OUT / "review.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "split", "category", "filename",
            "cap", "label", "fill_level", "num_detections", "flags",
        ])
        writer.writeheader()
        writer.writerows(review_rows)

    # Write data.yaml
    yaml_text = f"""# YOLO11 dataset config (PET bottle QC)
path: .
train: images/train
val: images/val

names:
  0: cap
  1: label
  2: fill_level
"""
    (OUT / "data.yaml").write_text(yaml_text)

    # Write README
    readme = """# PET Bottle Auto-Labeling Output

## 구성
- `images/train`, `images/val` — 원본 BMP 이미지 (80/20 split, 카테고리별 stratified)
- `labels/train`, `labels/val` — YOLO11 포맷 라벨 (`class_id cx cy w h`, 0–1 정규화)
- `previews/` — 박스가 그려진 시각화 이미지 (검수용)
- `review.csv` — 이미지별 검출 요약 및 플래그
- `data.yaml` — YOLO 학습에 바로 쓰는 config

## 클래스
- 0 = cap (캡)
- 1 = label (라벨)
- 2 = fill_level (충진량) — 캡 바로 아래(또는 meniscus)부터 병 바닥까지

## 검수 우선순위
`review.csv`의 `flags` 컬럼이 비어있지 않은 이미지부터 확인.
- `MISSING_CAP/LABEL` — 있어야 할 게 빠진 경우
- `UNEXPECTED_CAP/LABEL` — 없어야 할 게 잡힌 경우
- `MISSING_FILL` — 충진량 박스가 안 잡힌 경우 (드묾)

## 검수 도구
[LabelImg](https://github.com/HumanSignal/labelImg) 또는
[X-AnyLabeling](https://github.com/CVHub520/X-AnyLabeling)에
`images/train` 또는 `images/val` 폴더를 열면 자동으로 `.txt` 라벨이 로드됩니다.

## YOLO11 학습 예시
```bash
pip install ultralytics
yolo detect train data=data.yaml model=yolo11n.pt epochs=100 imgsz=1280
```

## 알려진 한계
- `충전량` 케이스에서 fill_level 박스의 상단은 실제 액체 표면이 아니라
  캡 바로 아래에서 시작합니다. 일관된 위치로 통일해서 모델이 박스 내부
  픽셀 패턴으로 fill 상태를 학습할 수 있도록 한 의도적 설계입니다.
  엄밀한 meniscus 감지가 필요하면 해당 이미지들은 수동 보정 권장.
- 라벨 박스는 라벨이 완전히 감싸지 않는 경우(라벨각도다름 등)
  미세한 조정이 필요할 수 있습니다.
"""
    (OUT / "README.md").write_text(readme)

    # Quick summary
    total = len(review_rows)
    flagged = sum(1 for r in review_rows if r["flags"])
    print(f"\n{'='*50}")
    print(f"DONE. {total} images processed.")
    print(f"Flagged for review: {flagged} ({100*flagged/total:.1f}%)")
    print(f"Output: {OUT.absolute()}")


if __name__ == "__main__":
    main()
