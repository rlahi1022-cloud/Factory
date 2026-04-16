"""test_inference.py
카메라 없이 이미지 파일로 추론서버를 테스트하는 스크립트.

사용법:
  cd AiServer

  # Station1 (입고검사) 테스트 — PatchCore
  python tests/test_inference.py --station 1 --image data/station1/test/sample.jpg

  # Station2 (조립검사) 테스트 — YOLO11 + PatchCore
  python tests/test_inference.py --station 2 --image data/station2/test/sample.jpg

  # 더미 이미지로 빠른 테스트 (모델 없이 동작 확인)
  python tests/test_inference.py --station 1 --dummy
  python tests/test_inference.py --station 2 --dummy
"""

import argparse
import sys
import time
from pathlib import Path

# AiServer 루트를 path에 추가
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import numpy as np


def create_dummy_image(width: int = 640, height: int = 480) -> np.ndarray:
    """테스트용 더미 이미지 생성 (BGR)."""
    img = np.random.randint(100, 200, (height, width, 3), dtype=np.uint8)
    return img


def test_station1(image: np.ndarray, model_path: str, threshold: float) -> None:
    """Station1 (입고검사) PatchCore 추론 테스트."""
    from Common.Config import StationConfig
    from Common.Inferencer import Station1Inferencer

    config = StationConfig(
        station_id=1,
        model_path=model_path,
        anomaly_threshold=threshold,
        patchcore_input_size=224,
    )

    inferencer = Station1Inferencer(config)
    inferencer.load_model()

    print(f"\n{'='*50}")
    print(f"  Station1 (입고검사) PatchCore 추론 테스트")
    print(f"{'='*50}")
    print(f"  이미지 크기: {image.shape}")
    print(f"  모델 경로:   {model_path}")
    print(f"  임계값:      {threshold}")
    print(f"{'='*50}")

    t0 = time.perf_counter()
    result = inferencer.infer(image)
    elapsed_ms = (time.perf_counter() - t0) * 1000

    print(f"\n  [결과]")
    print(f"  판정:       {result['result']}")
    print(f"  이상점수:   {result['score']}")
    print(f"  결함유형:   {result['defect'] or '(없음)'}")
    print(f"  히트맵:     {'있음' if result.get('heatmap') is not None else '없음'}")
    print(f"  추론시간:   {elapsed_ms:.1f} ms")
    print()

    if result.get("heatmap") is not None:
        try:
            import cv2
            cv2.imwrite("test_station1_heatmap.jpg", result["heatmap"])
            print("  히트맵 저장: test_station1_heatmap.jpg")
        except ImportError:
            pass


def test_station2(image: np.ndarray, yolo_path: str, patchcore_path: str,
                  threshold: float) -> None:
    """Station2 (조립검사) YOLO11 + PatchCore 하이브리드 추론 테스트."""
    from Common.Config import StationConfig
    from Common.Inferencer import Station2Inferencer

    config = StationConfig(
        station_id=2,
        model_path=yolo_path,
        patchcore_model_path=patchcore_path,
        anomaly_threshold=threshold,
        yolo_conf_threshold=0.5,
        yolo_iou_threshold=0.45,
        yolo_input_size=640,
        patchcore_input_size=224,
    )

    inferencer = Station2Inferencer(config)
    inferencer.load_model()

    print(f"\n{'='*50}")
    print(f"  Station2 (조립검사) YOLO11+PatchCore 테스트")
    print(f"{'='*50}")
    print(f"  이미지 크기:     {image.shape}")
    print(f"  YOLO 모델:       {yolo_path}")
    print(f"  PatchCore 모델:  {patchcore_path}")
    print(f"  임계값:          {threshold}")
    print(f"{'='*50}")

    t0 = time.perf_counter()
    result = inferencer.infer(image)
    elapsed_ms = (time.perf_counter() - t0) * 1000

    print(f"\n  [결과]")
    print(f"  판정:           {result['result']}")
    print(f"  총점수:         {result['score']}")
    print(f"  결함목록:       {result['defects'] or '(없음)'}")
    print(f"  캡 정상:        {result['cap_ok']}")
    print(f"  라벨 정상:      {result['label_ok']}")
    print(f"  충전량 정상:    {result['fill_ok']}")
    print(f"  PatchCore점수:  {result['patchcore_score']}")
    print(f"  YOLO 탐지수:    {len(result['detections'])}")
    print(f"  추론시간:       {elapsed_ms:.1f} ms")

    for i, det in enumerate(result["detections"]):
        print(f"    탐지 #{i+1}: {det['class']} conf={det['conf']} "
              f"bbox={det['bbox']} position_ok={det['position_ok']}")
    print()

    if result.get("bbox_overlay") is not None:
        try:
            import cv2
            cv2.imwrite("test_station2_overlay.jpg", result["bbox_overlay"])
            print("  오버레이 저장: test_station2_overlay.jpg")
        except ImportError:
            pass


def main():
    parser = argparse.ArgumentParser(description="AI 추론 테스트")
    parser.add_argument("--station", type=int, required=True, choices=[1, 2],
                        help="테스트할 스테이션 (1=입고, 2=조립)")
    parser.add_argument("--image", type=str, default="",
                        help="테스트 이미지 경로 (.jpg/.png)")
    parser.add_argument("--dummy", action="store_true",
                        help="더미 이미지로 테스트 (모델 없이 동작 확인)")
    parser.add_argument("--model", type=str, default="",
                        help="모델 파일 경로 (Station1: .ckpt, Station2: .pt)")
    parser.add_argument("--patchcore", type=str, default="",
                        help="Station2 전용: PatchCore 모델 경로 (.ckpt)")
    parser.add_argument("--threshold", type=float, default=0.5,
                        help="이상 점수 임계값 (기본: 0.5)")
    args = parser.parse_args()

    # 이미지 로드
    if args.dummy:
        image = create_dummy_image()
        print("더미 이미지 사용 (640x480 랜덤)")
    elif args.image:
        try:
            import cv2
            image = cv2.imread(args.image)
            if image is None:
                print(f"이미지 로드 실패: {args.image}")
                sys.exit(1)
            print(f"이미지 로드: {args.image}")
        except ImportError:
            print("opencv-python이 필요합니다: pip install opencv-python")
            sys.exit(1)
    else:
        print("--image 또는 --dummy 옵션을 지정하세요.")
        sys.exit(1)

    # 테스트 실행
    if args.station == 1:
        model_path = args.model or "./models/station1_patchcore.ckpt"
        test_station1(image, model_path, args.threshold)
    else:
        yolo_path = args.model or "./models/station2_yolo11.pt"
        patchcore_path = args.patchcore or "./models/station2_patchcore.ckpt"
        test_station2(image, yolo_path, patchcore_path, args.threshold)


if __name__ == "__main__":
    main()
