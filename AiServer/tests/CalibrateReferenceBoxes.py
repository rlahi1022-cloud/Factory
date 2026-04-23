"""CalibrateReferenceBoxes.py — YOLO 학습 라벨에서 정상 위치 기준 자동 계산

목적:
  Station2Inferencer 의 `_reference_boxes` (cap/label/liquid_level 의 정상 위치)
  를 실제 학습 데이터로부터 자동 계산한다.

동작:
  data/station2/yolo/labels/train/*.txt 를 모두 읽어,
  클래스별로 바운딩 박스의 (x_min/x_max/y_min/y_max) 평균 또는 ±1σ 범위를 산출.
  → 복사해서 Inferencer.py 의 _reference_boxes 에 붙여넣으면 됨.

사용법:
  cd /mnt/hdd/factory/code/AiServer
  python3 tests/CalibrateReferenceBoxes.py \
      --labels data/station2/yolo/labels/train

출력 예시:
  cap:          {"y_min": 0.05, "y_max": 0.22, "x_min": 0.35, "x_max": 0.65}
  label:        {"y_min": 0.28, "y_max": 0.70, "x_min": 0.18, "x_max": 0.82}
  liquid_level: {"y_min": 0.55, "y_max": 0.85, "x_min": 0.22, "x_max": 0.78}
"""

from __future__ import annotations

import argparse
from pathlib import Path
from statistics import mean, stdev


CLASS_NAMES = {0: "cap", 1: "label", 2: "liquid_level"}


def parse_yolo_label(path: Path) -> list[tuple[int, float, float, float, float]]:
    """YOLO 라벨 파일 한 개를 파싱.
    각 라인: class_id cx cy w h (정규화 0~1)
    반환: [(class_id, x_min, y_min, x_max, y_max), ...]
    """
    out = []
    try:
        for line in path.read_text().splitlines():
            parts = line.strip().split()
            if len(parts) != 5:
                continue
            cls = int(parts[0])
            cx, cy, w, h = map(float, parts[1:])
            x_min = cx - w / 2
            x_max = cx + w / 2
            y_min = cy - h / 2
            y_max = cy + h / 2
            out.append((cls, x_min, y_min, x_max, y_max))
    except Exception as exc:
        print(f"[WARN] 파싱 실패 {path}: {exc}")
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--labels", required=True,
                        help="YOLO 라벨 폴더 (예: data/station2/yolo/labels/train)")
    parser.add_argument("--margin", type=float, default=0.05,
                        help="평균에서 ±margin 여유 추가 (기본 0.05)")
    args = parser.parse_args()

    label_dir = Path(args.labels)
    if not label_dir.is_dir():
        print(f"[ERROR] 폴더 없음: {label_dir}")
        return

    # 클래스별 좌표 수집
    data: dict[int, list[tuple[float, float, float, float]]] = {0: [], 1: [], 2: []}
    n_files = 0
    for txt in sorted(label_dir.glob("*.txt")):
        n_files += 1
        for (cls, x1, y1, x2, y2) in parse_yolo_label(txt):
            if cls in data:
                data[cls].append((x1, y1, x2, y2))

    print(f"\n라벨 파일 {n_files} 개 처리 완료.\n")
    print("=" * 70)
    print(f"{'클래스':<14} {'x_min':>8} {'y_min':>8} {'x_max':>8} {'y_max':>8} {'샘플':>6}")
    print("=" * 70)

    # 각 클래스별 bounding box 의 평균 계산 + 여유 margin 추가
    result_lines = []
    for cls_id, cls_name in CLASS_NAMES.items():
        boxes = data[cls_id]
        if not boxes:
            print(f"{cls_name:<14} (데이터 없음)")
            continue

        # 평균 — 각 모서리의 평균
        avg_x1 = mean(b[0] for b in boxes)
        avg_y1 = mean(b[1] for b in boxes)
        avg_x2 = mean(b[2] for b in boxes)
        avg_y2 = mean(b[3] for b in boxes)

        # 여유 ±margin
        x_min = max(0.0, avg_x1 - args.margin)
        y_min = max(0.0, avg_y1 - args.margin)
        x_max = min(1.0, avg_x2 + args.margin)
        y_max = min(1.0, avg_y2 + args.margin)

        print(f"{cls_name:<14} {x_min:>8.3f} {y_min:>8.3f} {x_max:>8.3f} {y_max:>8.3f} {len(boxes):>6}")

        result_lines.append(
            f'    "{cls_name}": {{"y_min": {y_min:.3f}, "y_max": {y_max:.3f}, '
            f'"x_min": {x_min:.3f}, "x_max": {x_max:.3f}}},'
        )

    print("=" * 70)
    print("\n복사해서 Inferencer.py 의 _reference_boxes 에 교체하세요:\n")
    print("self._reference_boxes = {")
    for line in result_lines:
        print(line)
    print("}")


if __name__ == "__main__":
    main()
