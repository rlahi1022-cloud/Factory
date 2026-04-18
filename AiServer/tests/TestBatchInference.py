"""TestBatchInference.py — 폴더 내 모든 이미지를 일괄 추론하는 테스트 스크립트

왜 필요한가:
  - test 폴더의 이미지 수십 장을 한 번에 추론하여 OK/NG 결과를 요약한다.
  - 모델이 전체 데이터셋에서 얼마나 잘 동작하는지 빠르게 확인할 수 있다.

사용법:
  cd AiServer

  # Station1 test 폴더 전체 추론
  python tests/TestBatchInference.py --station 1 --dir data/station1/test --device cpu

  # Station2 test 폴더 전체 추론
  python tests/TestBatchInference.py --station 2 --dir data/station2/test --device cpu

  # 임계값 조정
  python tests/TestBatchInference.py --station 1 --dir data/station1/test --threshold 0.6
"""

# argparse: 커맨드라인 인자 파싱 라이브러리
import argparse
# sys: 시스템 경로 추가용
import sys
# time: 추론 시간 측정용
import time
# Path: 파일 경로를 객체로 다루기 위한 라이브러리
from pathlib import Path

# AiServer 루트를 파이썬 모듈 검색 경로에 추가
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# numpy: 이미지 배열 처리
import numpy as np


def collect_images(folder: Path) -> list[Path]:
    """폴더에서 이미지 파일들을 수집한다.

    목적:
      - 폴더 안의 모든 이미지 파일(.jpg, .png, .bmp)을 찾아 리스트로 반환한다.

    매개변수:
      folder (Path): 이미지가 있는 폴더 경로

    반환값:
      list[Path]: 이미지 파일 경로 리스트 (정렬됨)
    """
    # 지원하는 이미지 확장자 목록
    extensions = [".jpg", ".jpeg", ".png", ".bmp"]
    images = []
    # 각 확장자별로 파일을 찾는다
    for ext in extensions:
        # glob: 특정 패턴의 파일을 찾는다 (*.jpg, *.png 등)
        # 대문자 확장자도 찾기 위해 두 번 호출한다
        images.extend(folder.glob(f"*{ext}"))
        images.extend(folder.glob(f"*{ext.upper()}"))
    # 중복 제거 후 정렬한다
    return sorted(set(images))


def batch_test_station1(folder: Path, model_path: str, threshold: float,
                        device: str) -> None:
    """Station1 (PatchCore) 배치 추론 테스트.

    목적:
      - 폴더 전체 이미지를 순회하며 정상/불량 판정
      - 결과를 표로 요약 출력

    매개변수:
      folder (Path): 테스트 이미지 폴더
      model_path (str): PatchCore 모델 경로
      threshold (float): 이상 점수 임계값
      device (str): 추론 디바이스 (auto/cuda/cpu)
    """
    # 필요한 모듈 import
    import cv2
    from Common.Config import StationConfig
    from Common.Inferencer import Station1Inferencer

    # 설정 객체 생성
    config = StationConfig(
        station_id=1,
        model_path=model_path,
        device=device,
        anomaly_threshold=threshold,
        patchcore_input_size=224,
    )

    # 추론기 생성 + 모델 로드 (한 번만)
    inferencer = Station1Inferencer(config)
    inferencer.load_model()

    # 이미지 수집
    images = collect_images(folder)
    if not images:
        print(f"[오류] {folder}에 이미지가 없습니다.")
        return

    # 헤더 출력
    print(f"\n{'=' * 80}")
    print(f"  Station1 배치 추론 테스트")
    print(f"{'=' * 80}")
    print(f"  폴더:       {folder}")
    print(f"  이미지 수:  {len(images)}")
    print(f"  모델:       {model_path}")
    print(f"  임계값:     {threshold}")
    print(f"  디바이스:   {device}")
    print(f"{'=' * 80}\n")

    # 테이블 헤더
    print(f"  {'번호':>4} | {'파일명':<40} | {'판정':>4} | {'이상점수':>8} | {'결함':<15}")
    print("-" * 90)

    # 결과 통계를 저장할 변수
    ok_count = 0      # OK 판정 수
    ng_count = 0      # NG 판정 수
    total_time = 0.0  # 전체 추론 시간

    # 각 이미지에 대해 추론 실행
    for idx, img_path in enumerate(images, 1):
        # 이미지 로드 (한글 경로 대응)
        # cv2.imread는 한글 경로에서 실패할 수 있으므로 numpy로 우회
        try:
            img = cv2.imdecode(
                np.fromfile(str(img_path), dtype=np.uint8),
                cv2.IMREAD_COLOR
            )
        except Exception:
            img = cv2.imread(str(img_path))

        if img is None:
            # 이미지 로드 실패 시 스킵
            print(f"  {idx:>4} | {img_path.name:<40} | FAIL | (로드 실패)")
            continue

        # 추론 실행 + 시간 측정
        t0 = time.perf_counter()
        result = inferencer.infer(img)
        elapsed_ms = (time.perf_counter() - t0) * 1000
        total_time += elapsed_ms

        # 결과에서 값 추출
        verdict = result.get("result", "OK")
        score = result.get("score", 0.0)
        defect = result.get("defect", "") or "-"

        # 파일명이 너무 길면 자른다 (37자 + "...")
        name = img_path.name
        if len(name) > 40:
            name = name[:37] + "..."

        # 결과 한 줄 출력
        print(f"  {idx:>4} | {name:<40} | {verdict:>4} | {score:>8.4f} | {defect:<15}")

        # 통계 누적
        if verdict == "NG":
            ng_count += 1
        else:
            ok_count += 1

    # 최종 요약 출력
    total = len(images)
    print("-" * 90)
    print(f"\n{'=' * 80}")
    print(f"  최종 요약")
    print(f"{'=' * 80}")
    print(f"  총 이미지:      {total}장")
    print(f"  OK (정상):      {ok_count}장 ({ok_count / total * 100:.1f}%)")
    print(f"  NG (불량):      {ng_count}장 ({ng_count / total * 100:.1f}%)")
    print(f"  평균 추론시간:  {total_time / total:.1f} ms/장")
    print(f"  전체 소요시간:  {total_time / 1000:.1f} 초")
    print(f"{'=' * 80}\n")


def batch_test_station2(folder: Path, yolo_path: str, patchcore_path: str,
                        threshold: float, device: str) -> None:
    """Station2 (YOLO11 + PatchCore) 배치 추론 테스트.

    매개변수:
      folder (Path): 테스트 이미지 폴더
      yolo_path (str): YOLO11 모델 경로
      patchcore_path (str): PatchCore 모델 경로
      threshold (float): 이상 점수 임계값
      device (str): 추론 디바이스
    """
    import cv2
    from Common.Config import StationConfig
    from Common.Inferencer import Station2Inferencer

    # 설정 객체 생성
    config = StationConfig(
        station_id=2,
        model_path=yolo_path,
        patchcore_model_path=patchcore_path,
        device=device,
        anomaly_threshold=threshold,
        yolo_conf_threshold=0.5,
        yolo_iou_threshold=0.45,
        yolo_input_size=640,
        patchcore_input_size=224,
    )

    # 추론기 생성 + 모델 로드
    inferencer = Station2Inferencer(config)
    inferencer.load_model()

    # 이미지 수집
    images = collect_images(folder)
    if not images:
        print(f"[오류] {folder}에 이미지가 없습니다.")
        return

    # 헤더 출력
    print(f"\n{'=' * 100}")
    print(f"  Station2 배치 추론 테스트")
    print(f"{'=' * 100}")
    print(f"  폴더:       {folder}")
    print(f"  이미지 수:  {len(images)}")
    print(f"  YOLO:       {yolo_path}")
    print(f"  PatchCore:  {patchcore_path}")
    print(f"  디바이스:   {device}")
    print(f"{'=' * 100}\n")

    # 테이블 헤더
    print(f"  {'번호':>4} | {'파일명':<35} | {'판정':>4} | {'cap':>5} | {'label':>5} | {'fill':>5} | {'결함':<30}")
    print("-" * 110)

    # 통계 변수
    ok_count = 0
    ng_count = 0
    total_time = 0.0

    # 각 이미지 추론
    for idx, img_path in enumerate(images, 1):
        # 한글 경로 대응 이미지 로드
        try:
            img = cv2.imdecode(
                np.fromfile(str(img_path), dtype=np.uint8),
                cv2.IMREAD_COLOR
            )
        except Exception:
            img = cv2.imread(str(img_path))

        if img is None:
            print(f"  {idx:>4} | {img_path.name:<35} | FAIL | (로드 실패)")
            continue

        # 추론 실행
        t0 = time.perf_counter()
        result = inferencer.infer(img)
        elapsed_ms = (time.perf_counter() - t0) * 1000
        total_time += elapsed_ms

        # 결과 추출
        verdict = result.get("result", "OK")
        cap_ok = "O" if result.get("cap_ok", True) else "X"
        label_ok = "O" if result.get("label_ok", True) else "X"
        fill_ok = "O" if result.get("fill_ok", True) else "X"
        defects = ", ".join(result.get("defects", [])) or "-"

        # 긴 텍스트 자르기
        name = img_path.name
        if len(name) > 35:
            name = name[:32] + "..."
        if len(defects) > 30:
            defects = defects[:27] + "..."

        # 출력
        print(f"  {idx:>4} | {name:<35} | {verdict:>4} | {cap_ok:>5} | {label_ok:>5} | {fill_ok:>5} | {defects:<30}")

        # 통계
        if verdict == "NG":
            ng_count += 1
        else:
            ok_count += 1

    # 최종 요약
    total = len(images)
    print("-" * 110)
    print(f"\n{'=' * 100}")
    print(f"  최종 요약")
    print(f"{'=' * 100}")
    print(f"  총 이미지:      {total}장")
    print(f"  OK (정상):      {ok_count}장 ({ok_count / total * 100:.1f}%)")
    print(f"  NG (불량):      {ng_count}장 ({ng_count / total * 100:.1f}%)")
    print(f"  평균 추론시간:  {total_time / total:.1f} ms/장")
    print(f"  전체 소요시간:  {total_time / 1000:.1f} 초")
    print(f"{'=' * 100}\n")


def main():
    """메인 진입점. 커맨드라인 인자 파싱 후 배치 테스트 실행."""
    parser = argparse.ArgumentParser(description="AI 배치 추론 테스트")
    # --station: 1 또는 2
    parser.add_argument("--station", type=int, required=True, choices=[1, 2],
                        help="스테이션 번호 (1=입고, 2=조립)")
    # --dir: 테스트할 폴더 경로
    parser.add_argument("--dir", type=str, required=True,
                        help="테스트 이미지 폴더 경로")
    # --model: 메인 모델 경로
    parser.add_argument("--model", type=str, default="",
                        help="모델 경로 (Station1: .ckpt / Station2: .pt)")
    # --patchcore: Station2 전용
    parser.add_argument("--patchcore", type=str, default="",
                        help="Station2 PatchCore 모델 경로")
    # --threshold: 임계값
    parser.add_argument("--threshold", type=float, default=0.5,
                        help="이상 점수 임계값 (기본: 0.5)")
    # --device: 디바이스
    parser.add_argument("--device", type=str, default="auto",
                        choices=["auto", "cuda", "cpu"],
                        help="추론 디바이스 (기본: auto)")
    args = parser.parse_args()

    # 폴더 경로 Path 객체로 변환
    folder = Path(args.dir)
    if not folder.exists():
        print(f"[오류] 폴더가 없습니다: {folder}")
        sys.exit(1)

    # 스테이션별 테스트 실행
    if args.station == 1:
        model_path = args.model or "./models/station1_patchcore.ckpt"
        batch_test_station1(folder, model_path, args.threshold, args.device)
    else:
        yolo_path = args.model or "./models/station2_yolo11.pt"
        patchcore_path = args.patchcore or "./models/station2_patchcore.ckpt"
        batch_test_station2(folder, yolo_path, patchcore_path,
                            args.threshold, args.device)


if __name__ == "__main__":
    main()
