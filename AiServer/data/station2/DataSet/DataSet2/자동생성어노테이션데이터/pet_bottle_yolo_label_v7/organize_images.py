"""
사용자가 원본 이미지를 YOLO 학습 폴더 구조에 복사하는 헬퍼 스크립트.

사용법:
    1) 이 스크립트와 같은 폴더에 원본 이미지 폴더를 배치:
         ./your_image_root/
             2_normal/
             2_abnormal/캡없음/
             2_abnormal/라벨없음/
             2_abnormal/충전량/
             2_abnormal/캡미세하게열림/
             2_abnormal/라벨각도다름/
    2) 실행:
         python organize_images.py ./your_image_root

스크립트는 review.csv의 split 정보를 기준으로 각 이미지를
images/train/ 또는 images/val/ 로 복사합니다.
"""
import csv
import shutil
import sys
from pathlib import Path


CATEGORY_FOLDER = {
    "normal":          "2_normal",
    "abn_no_cap":      "2_abnormal/캡없음",
    "abn_cap_open":    "2_abnormal/캡미세하게열림",
    "abn_no_label":    "2_abnormal/라벨없음",
    "abn_label_angle": "2_abnormal/라벨각도다름",
    "abn_underfill":   "2_abnormal/충전량",
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    src_root = Path(sys.argv[1])
    if not src_root.exists():
        print(f"Source folder not found: {src_root}")
        sys.exit(1)

    review_path = Path("review.csv")
    if not review_path.exists():
        print("review.csv not found - run this from the dataset root.")
        sys.exit(1)

    for split in ("train", "val"):
        Path(f"images/{split}").mkdir(parents=True, exist_ok=True)

    n_ok = 0
    n_miss = 0
    with open(review_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            split = row["split"]
            category = row["category"]
            filename = row["filename"]
            # Original filename: strip the "{category}__" prefix
            orig_name = filename.split("__", 1)[1]
            src_subfolder = CATEGORY_FOLDER.get(category)
            if src_subfolder is None:
                print(f"Unknown category: {category}")
                n_miss += 1
                continue
            src_path = src_root / src_subfolder / orig_name
            dst_path = Path(f"images/{split}/{filename}")
            if not src_path.exists():
                print(f"Missing: {src_path}")
                n_miss += 1
                continue
            shutil.copy(src_path, dst_path)
            n_ok += 1

    print(f"\nCopied {n_ok} images, missing {n_miss}.")
    if n_miss == 0:
        print("\nAll images copied. YOLO training ready:")
        print("  pip install ultralytics")
        print("  yolo detect train data=data.yaml model=yolo11n.pt epochs=100 imgsz=1280")


if __name__ == "__main__":
    main()
