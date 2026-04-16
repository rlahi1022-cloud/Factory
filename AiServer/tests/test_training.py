"""test_training.py
학습서버 테스트 스크립트.

사용법:
  cd AiServer

  # PatchCore 학습 테스트 (Station1 입고검사)
  python tests/test_training.py --type patchcore --station 1 --data data/station1/normal

  # PatchCore 학습 테스트 (Station2 라벨표면)
  python tests/test_training.py --type patchcore --station 2 --data data/station2/patchcore/normal

  # YOLO11 학습 테스트 (Station2 조립검사)
  python tests/test_training.py --type yolo --data data/station2/yolo

  # 데이터 증강만 실행
  python tests/test_training.py --augment data/station1/normal --factor 5
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


def test_patchcore_training(station_id: int, data_dir: str) -> None:
    """PatchCore 학습 테스트."""
    from Training.TrainPatchcore import PatchcoreTrainer

    print(f"\n{'='*50}")
    print(f"  PatchCore 학습 테스트 (Station {station_id})")
    print(f"{'='*50}")
    print(f"  데이터 경로: {data_dir}")

    # 데이터 확인
    data_path = Path(data_dir)
    if not data_path.exists():
        print(f"\n  [오류] 데이터 폴더가 없습니다: {data_dir}")
        print(f"  아래 경로에 정상 이미지를 넣어주세요:")
        print(f"    {data_dir}/")
        print(f"    └── *.jpg, *.png, *.bmp (정상 이미지)")
        return

    images = list(data_path.glob("*.jpg")) + list(data_path.glob("*.png")) + list(data_path.glob("*.bmp"))
    # augmented 폴더도 확인
    aug_dir = data_path / "augmented"
    if aug_dir.exists():
        images += list(aug_dir.glob("*.jpg")) + list(aug_dir.glob("*.png"))

    print(f"  이미지 수:   {len(images)}")
    if len(images) == 0:
        print(f"\n  [오류] 이미지가 없습니다. 정상 이미지를 넣어주세요.")
        return

    def progress(info):
        print(f"  [{info['progress']:3d}%] {info['status']}")

    trainer = PatchcoreTrainer(
        station_id=station_id,
        data_dir=data_dir,
        output_dir="./models",
        backbone="wide_resnet50_2",
        input_size=224,
        batch_size=32,
        num_workers=4,
        device="cuda",
        progress_callback=progress,
    )

    print(f"\n  학습 시작...")
    result = trainer.train()

    print(f"\n  [결과]")
    print(f"  성공:     {result['success']}")
    print(f"  모델경로: {result['model_path']}")
    print(f"  버전:     {result['version']}")
    print(f"  정확도:   {result['accuracy']}")
    print(f"  메시지:   {result['message']}")
    print()


def test_yolo_training(data_dir: str) -> None:
    """YOLO11 학습 테스트."""
    from Training.TrainYolo import YoloTrainer, create_data_yaml

    print(f"\n{'='*50}")
    print(f"  YOLO11 학습 테스트 (Station 2)")
    print(f"{'='*50}")
    print(f"  데이터 경로: {data_dir}")

    data_path = Path(data_dir)
    train_img_dir = data_path / "images" / "train"
    train_lbl_dir = data_path / "labels" / "train"

    if not train_img_dir.exists() or not train_lbl_dir.exists():
        print(f"\n  [오류] YOLO 데이터셋 구조가 없습니다.")
        print(f"  아래 구조로 데이터를 준비해주세요:")
        print(f"    {data_dir}/")
        print(f"    ├── images/")
        print(f"    │   ├── train/  (학습 이미지 *.jpg)")
        print(f"    │   └── val/    (검증 이미지 *.jpg)")
        print(f"    └── labels/")
        print(f"        ├── train/  (라벨 *.txt, YOLO 포맷)")
        print(f"        └── val/    (라벨 *.txt)")
        print(f"\n  라벨 포맷 (각 줄): class_id cx cy w h")
        print(f"    0 = cap, 1 = label, 2 = liquid_level")
        print(f"  예시: 0 0.5 0.1 0.3 0.12")
        return

    train_images = list(train_img_dir.glob("*.jpg")) + list(train_img_dir.glob("*.png"))
    train_labels = list(train_lbl_dir.glob("*.txt"))
    print(f"  학습 이미지:  {len(train_images)}")
    print(f"  학습 라벨:    {len(train_labels)}")

    if len(train_images) == 0:
        print(f"\n  [오류] 학습 이미지가 없습니다.")
        return

    # data.yaml 생성
    data_yaml = str(data_path / "data.yaml")
    if not Path(data_yaml).exists():
        create_data_yaml(data_dir, data_yaml)
        print(f"  data.yaml 생성: {data_yaml}")

    def progress(info):
        print(f"  [{info['progress']:3d}%] {info['status']}")

    trainer = YoloTrainer(
        data_yaml=data_yaml,
        output_dir="./models",
        base_model="yolo11n.pt",
        input_size=640,
        epochs=100,
        batch_size=16,
        patience=20,
        device="cuda",
        progress_callback=progress,
    )

    print(f"\n  학습 시작...")
    result = trainer.train()

    print(f"\n  [결과]")
    print(f"  성공:     {result['success']}")
    print(f"  모델경로: {result['model_path']}")
    print(f"  버전:     {result['version']}")
    print(f"  정확도:   {result['accuracy']}")
    print(f"  메시지:   {result['message']}")
    print()


def test_augmentation(data_dir: str, factor: int) -> None:
    """데이터 증강 테스트."""
    from Training.TrainPatchcore import augment_dataset

    print(f"\n{'='*50}")
    print(f"  데이터 증강 테스트")
    print(f"{'='*50}")
    print(f"  데이터 경로: {data_dir}")
    print(f"  증강 배수:   {factor}x")

    data_path = Path(data_dir)
    images = list(data_path.glob("*.jpg")) + list(data_path.glob("*.png")) + list(data_path.glob("*.bmp"))
    print(f"  원본 이미지: {len(images)}")

    if len(images) == 0:
        print(f"\n  [오류] 이미지가 없습니다.")
        return

    augment_dataset(data_dir, factor=factor)

    aug_dir = data_path / "augmented"
    aug_images = list(aug_dir.glob("*.jpg")) + list(aug_dir.glob("*.png"))
    print(f"  증강 이미지: {len(aug_images)} (in {aug_dir})")
    print()


def main():
    parser = argparse.ArgumentParser(description="AI 학습 테스트")
    parser.add_argument("--type", choices=["patchcore", "yolo"],
                        help="학습 유형")
    parser.add_argument("--station", type=int, default=1, choices=[1, 2],
                        help="스테이션 번호 (PatchCore용)")
    parser.add_argument("--data", type=str, default="",
                        help="데이터 경로")
    parser.add_argument("--augment", type=str, default="",
                        help="증강할 데이터 폴더 경로")
    parser.add_argument("--factor", type=int, default=5,
                        help="증강 배수 (기본: 5)")
    args = parser.parse_args()

    if args.augment:
        test_augmentation(args.augment, args.factor)
    elif args.type == "patchcore":
        data = args.data or f"./data/station{args.station}/normal"
        test_patchcore_training(args.station, data)
    elif args.type == "yolo":
        data = args.data or "./data/station2/yolo"
        test_yolo_training(data)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
