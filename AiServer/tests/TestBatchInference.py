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


def make_anomalib_visualization(original: np.ndarray, model: Any,
                                 input_tensor: "torch.Tensor") -> np.ndarray:
    """Anomalib 공식 시각화 기능을 사용하여 3분할 이미지 생성.

    Anomalib의 ImageVisualizer가 학습 시 저장하는 것과 동일한 이미지를 생성한다.

    매개변수:
      original (ndarray): 원본 이미지 (BGR)
      model: Anomalib PatchCore 모델
      input_tensor (Tensor): 전처리된 입력 텐서 (1, 3, 224, 224)

    반환값:
      ndarray: 3분할 시각화 이미지 (BGR) 또는 None (실패 시)
    """
    try:
        import torch
        import cv2
        # Anomalib 시각화 도구
        from anomalib.visualization import ImageVisualizer

        visualizer = ImageVisualizer()
        with torch.no_grad():
            # 모델 전체 호출 (PostProcessor 적용된 InferenceBatch 반환)
            output = model(input_tensor)

        # ImageVisualizer는 Batch 객체를 기대하므로 wrapping
        # Anomalib 버전에 따라 호출 방식이 다를 수 있어 여러 방법 시도
        try:
            vis_result = visualizer.visualize(output)
            if isinstance(vis_result, list) and len(vis_result) > 0:
                vis_img = vis_result[0]
            else:
                vis_img = vis_result
        except Exception:
            return None

        # PIL/Tensor/numpy 중 어느 형태든 numpy BGR로 변환
        if hasattr(vis_img, "numpy"):
            vis_img = vis_img.numpy()
        elif hasattr(vis_img, "__array__"):
            vis_img = np.array(vis_img)
        if vis_img.dtype != np.uint8:
            vis_img = (vis_img * 255).clip(0, 255).astype(np.uint8)
        if vis_img.shape[-1] == 3:  # RGB → BGR
            vis_img = cv2.cvtColor(vis_img, cv2.COLOR_RGB2BGR)
        return vis_img
    except Exception:
        return None


def make_visualization(original: np.ndarray, heatmap: np.ndarray,
                       threshold: float, score: float, verdict: str,
                       raw_anomaly_map: np.ndarray = None,
                       pixel_threshold: float = None,
                       pred_mask: np.ndarray = None) -> np.ndarray:
    """Anomalib 스타일 3분할 시각화 이미지를 생성한다.

    [원본] | [원본 + 히트맵] | [원본 + Pred Mask]

    매개변수:
      original (ndarray): 원본 이미지 (BGR)
      heatmap (ndarray): 이상 점수 히트맵 (컬러맵 적용된 BGR)
      threshold (float): 이상 점수 임계값
      score (float): 이 이미지의 anomaly score
      verdict (str): "OK" 또는 "NG"
      raw_anomaly_map (ndarray): 원본 수치 맵 (float32, 마스크 계산용)

    반환값:
      ndarray: 3분할로 이어 붙인 이미지 (BGR)
    """
    import cv2
    h, w = original.shape[:2]

    # ── 1. 원본 이미지 ──
    panel1 = original.copy()

    # ── 2. 원본 + 히트맵 오버레이 ──
    if heatmap is not None:
        if heatmap.shape[:2] != (h, w):
            heatmap_resized = cv2.resize(heatmap, (w, h))
        else:
            heatmap_resized = heatmap
        panel2 = cv2.addWeighted(original, 0.6, heatmap_resized, 0.4, 0)
    else:
        panel2 = (original * 0.5).astype(np.uint8)

    # ── 3. 원본 + Pred Mask (이상 영역만 정확하게 표시) ──
    panel3 = original.copy()
    # 접근 방식 (Anomalib과 동일 원리):
    #   raw_anomaly_map을 min-max 정규화 (히트맵과 동일 기준)
    #   → 정규화된 값 > 0.5인 픽셀을 마스크로 표시
    #   → 히트맵에서 빨간-노랑 영역이 곧 Pred Mask가 된다
    mask_source = None
    if raw_anomaly_map is not None and verdict == "NG":
        if raw_anomaly_map.shape[:2] != (h, w):
            raw_map = cv2.resize(raw_anomaly_map, (w, h))
        else:
            raw_map = raw_anomaly_map

        # 백분위수 기반 임계값 (상위 5%만 마스크)
        # 상위 5%만 잡으면 파이리(큰 결함)도 중심부만, 보드마카(작은 결함)도 정확히 포착
        # percentile은 이미지 분포에 상관없이 일관된 비율 마스크를 만든다
        percentile_threshold = float(np.percentile(raw_map, 95))

        # 추가 안전장치: 정규화 50% 이상 조건도 함께 적용 (AND)
        # → 배경이 노랑에 가까운 이미지에서 과도한 마스크 방지
        map_min = float(raw_map.min())
        map_max = float(raw_map.max())
        if map_max > map_min:
            normalized = (raw_map - map_min) / (map_max - map_min)
            # 두 조건 모두 만족하는 픽셀만 마스크
            mask_source = ((raw_map >= percentile_threshold) &
                           (normalized >= 0.7)).astype(np.uint8) * 255

    # 마스크가 있으면 윤곽선 추출 후 빨간 테두리 그리기
    if mask_source is not None:
        # 마스크 노이즈 제거 (작은 구멍 메우고 작은 점 제거)
        kernel = np.ones((5, 5), np.uint8)
        mask_clean = cv2.morphologyEx(mask_source, cv2.MORPH_CLOSE, kernel)
        mask_clean = cv2.morphologyEx(mask_clean, cv2.MORPH_OPEN, kernel)
        contours, _ = cv2.findContours(mask_clean, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        cv2.drawContours(panel3, contours, -1, (0, 0, 255), 3)

    # ── 각 패널에 텍스트 라벨 추가 ──
    labels = ["Image", "Image + Anomaly Map", "Image + Pred Mask"]
    panels = [panel1, panel2, panel3]
    for panel, label in zip(panels, labels):
        # 반투명 배경 + 흰색 텍스트 (가독성)
        overlay = panel.copy()
        cv2.rectangle(overlay, (10, 10), (10 + len(label) * 13, 45), (0, 0, 0), -1)
        cv2.addWeighted(overlay, 0.6, panel, 0.4, 0, panel)
        cv2.putText(panel, label, (15, 35), cv2.FONT_HERSHEY_SIMPLEX,
                    0.7, (255, 255, 255), 2)

    # ── 3개 패널을 가로로 이어붙이기 ──
    combined = np.hstack([panel1, panel2, panel3])

    # 상단에 판정 결과 띠 추가 (녹색=OK, 빨강=NG)
    banner_h = 50
    banner_color = (0, 180, 0) if verdict == "OK" else (0, 0, 220)
    banner = np.full((banner_h, combined.shape[1], 3), banner_color, dtype=np.uint8)
    banner_text = f"{verdict}  |  Score: {score:.4f}  |  Threshold: {threshold:.4f}"
    cv2.putText(banner, banner_text, (20, 35), cv2.FONT_HERSHEY_SIMPLEX,
                0.9, (255, 255, 255), 2)

    # 배너 + 3분할 이미지 세로 결합
    return np.vstack([banner, combined])


def save_image_with_korean_path(path: Path, image: np.ndarray) -> bool:
    """한글 경로 대응 이미지 저장 (cv2.imwrite 한글 실패 우회).

    매개변수:
      path (Path): 저장 경로
      image (ndarray): 저장할 이미지

    반환값:
      bool: 저장 성공 여부
    """
    import cv2
    try:
        # imencode → 바이트 → tofile 방식으로 한글 경로 우회
        ext = path.suffix or ".png"
        success, buf = cv2.imencode(ext, image)
        if success:
            buf.tofile(str(path))
            return True
    except Exception:
        pass
    # fallback: cv2.imwrite 직접 시도
    try:
        return cv2.imwrite(str(path), image)
    except Exception:
        return False


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
                        device: str, save_vis: bool = True,
                        vis_dir: str = "test_results") -> None:
    """Station1 (PatchCore) 배치 추론 테스트.

    목적:
      - 폴더 전체 이미지를 순회하며 정상/불량 판정
      - 결과를 표로 요약 출력
      - save_vis=True이면 각 이미지에 대해 Anomalib 스타일 3분할 시각화 저장

    매개변수:
      folder (Path): 테스트 이미지 폴더
      model_path (str): PatchCore 모델 경로
      threshold (float): 이상 점수 임계값
      device (str): 추론 디바이스 (auto/cuda/cpu)
      save_vis (bool): True면 모든 이미지에 대해 시각화 결과 저장
      vis_dir (str): 시각화 결과 저장 루트 폴더
    """
    # 필요한 모듈 import
    import csv
    import cv2
    from datetime import datetime
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

    # ── 시각화 폴더 생성 ──
    # save_vis=True면 타임스탬프 기반 폴더 생성 (결과가 섞이지 않도록)
    vis_output_dir = None
    csv_path = None
    csv_file = None
    csv_writer = None
    if save_vis:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        vis_output_dir = Path(vis_dir) / f"station1_{timestamp}"
        vis_output_dir.mkdir(parents=True, exist_ok=True)
        # CSV 요약 파일 준비
        csv_path = vis_output_dir / "summary.csv"
        csv_file = open(csv_path, "w", newline="", encoding="utf-8-sig")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["번호", "파일명", "판정", "이상점수", "결함", "추론시간(ms)"])

    # 헤더 출력
    print(f"\n{'=' * 80}")
    print(f"  Station1 배치 추론 테스트")
    print(f"{'=' * 80}")
    print(f"  폴더:       {folder}")
    print(f"  이미지 수:  {len(images)}")
    print(f"  모델:       {model_path}")
    print(f"  임계값:     {threshold}")
    print(f"  디바이스:   {device}")
    if save_vis:
        print(f"  시각화:     {vis_output_dir}")
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

        # ── 시각화 이미지 저장 ──
        if save_vis and vis_output_dir is not None:
            # 3분할 시각화 이미지 생성
            heatmap = result.get("heatmap")
            raw_map = result.get("raw_anomaly_map")          # fallback용 raw 수치
            pix_thr = result.get("pixel_threshold")          # Anomalib 내부 pixel threshold
            pred_mask = result.get("pred_mask")              # Anomalib PostProcessor 결과 (최우선)
            vis_img = make_visualization(img, heatmap, threshold, score, verdict,
                                         raw_anomaly_map=raw_map,
                                         pixel_threshold=pix_thr,
                                         pred_mask=pred_mask)
            # 파일명: "번호_원본명_판정_점수.png"
            # 예: "001_Image_2026-04-17_NG_74.21.png"
            safe_name = img_path.stem.replace(" ", "_")[:40]
            vis_filename = f"{idx:03d}_{safe_name}_{verdict}_{score:.2f}.png"
            vis_path = vis_output_dir / vis_filename
            save_image_with_korean_path(vis_path, vis_img)

            # CSV에 요약 기록
            if csv_writer:
                csv_writer.writerow([
                    idx, img_path.name, verdict, f"{score:.4f}",
                    defect, f"{elapsed_ms:.1f}",
                ])

        # 통계 누적
        if verdict == "NG":
            ng_count += 1
        else:
            ok_count += 1

    # CSV 파일 닫기
    if csv_file is not None:
        csv_file.close()

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
    if save_vis and vis_output_dir:
        print(f"  시각화 저장:    {vis_output_dir}")
        print(f"  CSV 요약:       {csv_path}")
    print(f"{'=' * 80}\n")


def batch_test_station2(folder: Path, yolo_path: str, patchcore_path: str,
                        threshold: float, device: str,
                        save_vis: bool = True,
                        vis_dir: str = "test_results") -> None:
    """Station2 (YOLO11 + PatchCore) 배치 추론 테스트.

    매개변수:
      folder (Path): 테스트 이미지 폴더
      yolo_path (str): YOLO11 모델 경로
      patchcore_path (str): PatchCore 모델 경로
      threshold (float): 이상 점수 임계값
      device (str): 추론 디바이스
      save_vis (bool): 시각화 이미지 저장 여부
      vis_dir (str): 저장 폴더
    """
    import csv
    import cv2
    from datetime import datetime
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

    # ── 시각화 폴더 생성 ──
    vis_output_dir = None
    csv_path = None
    csv_file = None
    csv_writer = None
    if save_vis:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        vis_output_dir = Path(vis_dir) / f"station2_{timestamp}"
        vis_output_dir.mkdir(parents=True, exist_ok=True)
        csv_path = vis_output_dir / "summary.csv"
        csv_file = open(csv_path, "w", newline="", encoding="utf-8-sig")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["번호", "파일명", "판정", "cap", "label", "fill",
                             "patchcore점수", "결함", "추론시간(ms)"])

    # 헤더 출력
    print(f"\n{'=' * 100}")
    print(f"  Station2 배치 추론 테스트")
    print(f"{'=' * 100}")
    print(f"  폴더:       {folder}")
    print(f"  이미지 수:  {len(images)}")
    print(f"  YOLO:       {yolo_path}")
    print(f"  PatchCore:  {patchcore_path}")
    print(f"  디바이스:   {device}")
    if save_vis:
        print(f"  시각화:     {vis_output_dir}")
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
        score = result.get("score", 0.0)
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

        # ── 시각화 이미지 저장 (Station2) ──
        if save_vis and vis_output_dir is not None:
            # Station2는 bbox_overlay(YOLO 박스 그려진 이미지)를 활용
            # patchcore_score로 히트맵 대용 점수 제공
            bbox_overlay = result.get("bbox_overlay")
            patchcore_score = result.get("patchcore_score", 0.0)

            # bbox 오버레이가 있으면 그걸 사용, 없으면 원본
            display_img = bbox_overlay if bbox_overlay is not None else img.copy()

            # YOLO + PatchCore 정보를 이미지 하단에 텍스트로 추가
            h, w = display_img.shape[:2]
            info_h = 80
            info_panel = np.full((info_h, w, 3), (30, 30, 30), dtype=np.uint8)
            info_lines = [
                f"cap: {cap_ok}  label: {label_ok}  fill: {fill_ok}",
                f"PatchCore score: {patchcore_score:.4f}  Threshold: {threshold:.4f}",
                f"Defects: {defects}",
            ]
            for i, line in enumerate(info_lines):
                cv2.putText(info_panel, line, (15, 25 + i * 20),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
            display_img = np.vstack([display_img, info_panel])

            # 상단 판정 배너 추가 (녹색/빨강)
            banner_h = 50
            banner_color = (0, 180, 0) if verdict == "OK" else (0, 0, 220)
            banner = np.full((banner_h, display_img.shape[1], 3), banner_color, dtype=np.uint8)
            banner_text = f"{verdict}  |  Score: {score:.4f}  |  Station2 (Assembly)"
            cv2.putText(banner, banner_text, (20, 35), cv2.FONT_HERSHEY_SIMPLEX,
                        0.9, (255, 255, 255), 2)
            vis_img = np.vstack([banner, display_img])

            # 파일 저장
            safe_name = img_path.stem.replace(" ", "_")[:40]
            vis_filename = f"{idx:03d}_{safe_name}_{verdict}_{score:.2f}.png"
            vis_path = vis_output_dir / vis_filename
            save_image_with_korean_path(vis_path, vis_img)

            # CSV 기록
            if csv_writer:
                csv_writer.writerow([
                    idx, img_path.name, verdict, cap_ok, label_ok, fill_ok,
                    f"{patchcore_score:.4f}", defects, f"{elapsed_ms:.1f}",
                ])

        # 통계
        if verdict == "NG":
            ng_count += 1
        else:
            ok_count += 1

    # CSV 파일 닫기
    if csv_file is not None:
        csv_file.close()

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
    if save_vis and vis_output_dir:
        print(f"  시각화 저장:    {vis_output_dir}")
        print(f"  CSV 요약:       {csv_path}")
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
    # --no-vis: 시각화 저장 비활성화 (기본은 저장)
    parser.add_argument("--no-vis", action="store_true",
                        help="시각화 이미지 저장 안 함 (기본: 저장함)")
    # --vis-dir: 시각화 결과 저장 폴더
    parser.add_argument("--vis-dir", type=str, default="test_results",
                        help="시각화 저장 폴더 (기본: test_results)")
    args = parser.parse_args()

    # 폴더 경로 Path 객체로 변환
    folder = Path(args.dir)
    if not folder.exists():
        print(f"[오류] 폴더가 없습니다: {folder}")
        sys.exit(1)

    # 시각화 저장 여부 (--no-vis 옵션이 없으면 저장)
    save_vis = not args.no_vis

    # 스테이션별 테스트 실행
    if args.station == 1:
        model_path = args.model or "./models/station1_patchcore.ckpt"
        batch_test_station1(folder, model_path, args.threshold, args.device,
                            save_vis=save_vis, vis_dir=args.vis_dir)
    else:
        yolo_path = args.model or "./models/station2_yolo11.pt"
        patchcore_path = args.patchcore or "./models/station2_patchcore.ckpt"
        batch_test_station2(folder, yolo_path, patchcore_path,
                            args.threshold, args.device,
                            save_vis=save_vis, vis_dir=args.vis_dir)


if __name__ == "__main__":
    main()
