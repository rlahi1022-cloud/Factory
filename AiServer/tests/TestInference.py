"""test_inference.py
카메라 없이 이미지 파일로 추론서버를 테스트하는 스크립트.

왜 필요한가:
  - 실제 공장 카메라 없이도 AI 모델의 추론(예측) 기능을 검증할 수 있다.
  - 개발 단계에서 모델이 정상적으로 동작하는지 빠르게 확인하는 용도이다.
  - 더미(가짜) 이미지를 생성해서 모델 없이도 코드 흐름을 테스트할 수 있다.

사용법:
  cd AiServer

  # Station1 (입고검사) 테스트 — PatchCore
  #   PatchCore는 정상 이미지만 학습해서 이상(불량)을 탐지하는 비지도학습 모델이다.
  python tests/test_inference.py --station 1 --image data/station1/test/sample.jpg

  # Station2 (조립검사) 테스트 — YOLO11 + PatchCore
  #   YOLO11은 객체 탐지(캡/라벨/충전량 위치 찾기), PatchCore는 표면 이상 탐지를 담당한다.
  python tests/test_inference.py --station 2 --image data/station2/test/sample.jpg

  # 더미 이미지로 빠른 테스트 (모델 없이 동작 확인)
  #   모델 파일이 없어도 코드가 정상 실행되는지 확인할 수 있다.
  python tests/test_inference.py --station 1 --dummy
  python tests/test_inference.py --station 2 --dummy
"""

# argparse: 커맨드라인에서 --station, --image 같은 인자를 파싱(분석)하기 위한 표준 라이브러리
import argparse
# sys: 파이썬 시스템 관련 기능 (경로 추가, 프로그램 종료 등)을 제공하는 표준 라이브러리
import sys
# time: 추론 소요 시간을 측정하기 위해 사용하는 표준 라이브러리
import time
# Path: 운영체제에 상관없이 파일/폴더 경로를 다루기 위한 객체지향 경로 라이브러리
from pathlib import Path

# AiServer 루트 폴더를 파이썬 모듈 검색 경로(sys.path)의 맨 앞에 추가한다.
# 왜: tests 폴더 안에서 실행해도 상위 폴더의 Common, Training 등의 모듈을 import할 수 있게 하기 위함이다.
# __file__은 현재 파일의 경로, .parent.parent는 두 단계 상위 폴더(AiServer 루트)를 가리킨다.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# numpy: 다차원 배열(행렬) 연산을 위한 핵심 라이브러리. 이미지 데이터는 numpy 배열로 표현된다.
# np라는 별명(alias)으로 사용하는 것이 관례이다.
import numpy as np


def create_dummy_image(width: int = 640, height: int = 480) -> np.ndarray:
    """테스트용 더미(가짜) 이미지를 생성하는 함수.

    목적:
      - 실제 이미지 파일이 없을 때 테스트를 위해 랜덤 픽셀값으로 가짜 이미지를 만든다.
      - 모델이 없어도 코드의 전체 흐름(이미지 로드 → 추론 → 결과 출력)이 동작하는지 확인하는 용도이다.

    매개변수:
      width (int): 생성할 이미지의 가로 픽셀 수. 기본값은 640.
      height (int): 생성할 이미지의 세로 픽셀 수. 기본값은 480.

    반환값:
      np.ndarray: BGR 형식의 3채널 이미지 배열. shape은 (height, width, 3)이다.
                  OpenCV는 RGB가 아닌 BGR 색상 순서를 사용한다.
    """
    # np.random.randint: 100~199 범위의 랜덤 정수를 생성하여 (높이, 너비, 3채널) 형태의 배열을 만든다.
    # 왜 100~200인가: 너무 어둡거나(0) 너무 밝은(255) 이미지를 피하고, 실제 이미지와 비슷한 밝기 범위를 사용하기 위함이다.
    # dtype=np.uint8: 각 픽셀값을 0~255 범위의 부호 없는 8비트 정수로 저장한다 (이미지 표준 형식).
    img = np.random.randint(100, 200, (height, width, 3), dtype=np.uint8)
    # 생성한 더미 이미지 배열을 반환한다.
    return img


def test_station1(image: np.ndarray, model_path: str, threshold: float,
                  device: str = "auto") -> None:
    """Station1 (입고검사) PatchCore 추론을 테스트하는 함수.

    목적:
      - 빈 용기(페트병)의 외관 이상을 탐지하는 PatchCore 모델의 추론을 테스트한다.
      - PatchCore는 정상 이미지의 특징을 기억해두고, 입력 이미지가 정상과 얼마나 다른지 점수(anomaly score)를 매긴다.

    매개변수:
      image (np.ndarray): 테스트할 입력 이미지 (BGR 형식의 numpy 배열).
      model_path (str): 학습된 PatchCore 모델 파일(.ckpt)의 경로.
      threshold (float): 이상 점수 임계값. 이 값을 넘으면 불량(NG)으로 판정한다.
      device (str): 추론에 사용할 장치. "auto"면 GPU가 있으면 GPU, 없으면 CPU를 자동 선택한다.

    반환값:
      None: 결과를 콘솔에 출력만 하고 값을 반환하지 않는다.
    """
    # StationConfig: 각 스테이션의 설정(모델 경로, 임계값 등)을 담는 설정 클래스를 가져온다.
    from Common.Config import StationConfig
    # Station1Inferencer: Station1 전용 PatchCore 추론기를 가져온다.
    from Common.Inferencer import Station1Inferencer

    # Station1에 필요한 설정 객체를 생성한다.
    # 왜 설정 객체를 따로 만드는가: 설정값들을 하나로 묶어 관리하면 코드가 깔끔하고 확장이 쉽다.
    config = StationConfig(
        station_id=1,                    # 스테이션 번호 1 = 입고검사 공정
        model_path=model_path,           # PatchCore 학습 모델 파일 경로
        device=device,                   # 추론 장치 (auto/cuda/cpu)
        anomaly_threshold=threshold,     # 불량 판정 기준 점수 (이 값 이상이면 불량)
        patchcore_input_size=224,        # PatchCore 모델에 입력되는 이미지 크기 (224x224 픽셀)
    )

    # 설정을 기반으로 Station1 추론기 인스턴스(객체)를 생성한다.
    inferencer = Station1Inferencer(config)
    # 모델 파일을 메모리에 로드(적재)한다. 이 과정에서 가중치(weights)가 로드된다.
    inferencer.load_model()

    # 테스트 정보를 보기 좋게 콘솔에 출력한다. '='*50은 구분선을 만든다.
    print(f"\n{'='*50}")
    print(f"  Station1 (입고검사) PatchCore 추론 테스트")
    print(f"{'='*50}")
    # image.shape는 (높이, 너비, 채널수)를 반환한다. 예: (480, 640, 3)
    print(f"  이미지 크기: {image.shape}")
    print(f"  모델 경로:   {model_path}")
    print(f"  임계값:      {threshold}")
    print(f"{'='*50}")

    # time.perf_counter(): 고정밀 시간을 측정한다. 추론 전 시각을 기록한다.
    # 왜 perf_counter인가: time.time()보다 정밀도가 높아 짧은 시간 측정에 적합하다.
    t0 = time.perf_counter()
    # 이미지를 모델에 넣어 추론(예측)을 실행한다. 결과는 딕셔너리로 반환된다.
    result = inferencer.infer(image)
    # 추론 후 시각에서 시작 시각을 빼고, 1000을 곱해 밀리초(ms) 단위로 변환한다.
    elapsed_ms = (time.perf_counter() - t0) * 1000

    # 추론 결과를 콘솔에 출력한다.
    print(f"\n  [결과]")
    # result['result']: 최종 판정 문자열 (예: "OK" 또는 "NG")
    print(f"  판정:       {result['result']}")
    # result['score']: PatchCore가 계산한 이상 점수 (높을수록 불량 가능성 높음)
    print(f"  이상점수:   {result['score']}")
    # result['defect']: 감지된 결함 유형. 없으면 None이므로 '(없음)'을 출력한다.
    print(f"  결함유형:   {result['defect'] or '(없음)'}")
    # 시각화용 raw 데이터가 있는지 확인 — 실제 MainServer 전송과 동일한 조건
    print(f"  히트맵:     {'있음' if result.get('raw_anomaly_map') is not None else '없음'}")
    print(f"  Pred Mask:  {'있음' if result.get('pred_mask') is not None else '없음'}")
    # 추론에 걸린 시간을 소수점 1자리까지 밀리초 단위로 출력한다.
    print(f"  추론시간:   {elapsed_ms:.1f} ms")
    # 빈 줄을 출력하여 가독성을 높인다.
    print()

    # ── 3장 파일 저장 ──
    # 실운영(StationRunner → MainServer → MFC)과 완전히 동일한 포맷으로 출력:
    #   test_station1_original.jpg   — 원본 JPEG
    #   test_station1_heatmap.png    — 원본 + Anomaly Map 오버레이 (JET 컬러맵)
    #   test_station1_mask.png       — 원본 + Pred Mask 빨간 윤곽선
    # 실제 MainServer가 받는 heatmap_bytes / pred_mask_bytes와 바이트 단위로 동일.
    try:
        import cv2
        from Common.Visualizer import (
            make_heatmap_overlay,
            make_pred_mask_overlay,
            encode_image,
        )

        # 1) 원본 저장 (JPEG)
        orig_bytes = encode_image(image, ".jpg", quality=90)
        if orig_bytes:
            Path("test_station1_original.jpg").write_bytes(orig_bytes)
            print(f"  원본 저장:     test_station1_original.jpg ({len(orig_bytes)} bytes)")

        # 2) 히트맵 오버레이 — raw_anomaly_map 사용 (NG 판정과 무관하게 생성)
        raw_map = result.get("raw_anomaly_map")
        if raw_map is not None:
            heatmap_img = make_heatmap_overlay(image, raw_map, alpha=0.5)
            heatmap_bytes = encode_image(heatmap_img, ".png")
            if heatmap_bytes:
                Path("test_station1_heatmap.png").write_bytes(heatmap_bytes)
                print(f"  히트맵 저장:   test_station1_heatmap.png ({len(heatmap_bytes)} bytes)")

        # 3) Pred Mask 윤곽선 — 빨간 테두리
        pred_mask = result.get("pred_mask")
        if pred_mask is not None:
            mask_img = make_pred_mask_overlay(image, pred_mask)
            mask_bytes = encode_image(mask_img, ".png")
            if mask_bytes:
                Path("test_station1_mask.png").write_bytes(mask_bytes)
                print(f"  마스크 저장:   test_station1_mask.png ({len(mask_bytes)} bytes)")
    except ImportError as exc:
        # OpenCV 미설치 또는 Visualizer import 실패 시 저장을 건너뛴다.
        print(f"  [경고] 시각화 저장 건너뜀: {exc}")


def test_station2(image: np.ndarray, yolo_path: str, patchcore_path: str,
                  threshold: float, device: str = "auto") -> None:
    """Station2 (조립검사) YOLO11 + PatchCore 하이브리드 추론을 테스트하는 함수.

    목적:
      - 조립 완성품(캡, 라벨, 충전량이 있는 음료병)의 품질을 검사한다.
      - YOLO11: 캡/라벨/충전량의 위치와 존재 여부를 탐지(객체 탐지)한다.
      - PatchCore: 라벨 표면의 미세 이상(스크래치, 오염 등)을 탐지한다.
      - 두 모델을 함께 사용하는 '하이브리드' 방식으로 더 정확한 검사를 수행한다.

    매개변수:
      image (np.ndarray): 테스트할 입력 이미지 (BGR 형식의 numpy 배열).
      yolo_path (str): 학습된 YOLO11 모델 파일(.pt)의 경로.
      patchcore_path (str): 학습된 PatchCore 모델 파일(.ckpt)의 경로.
      threshold (float): 이상 점수 임계값. 이 값을 넘으면 불량(NG)으로 판정한다.
      device (str): 추론에 사용할 장치. "auto"면 자동 선택, "cuda"면 GPU, "cpu"면 CPU를 사용한다.

    반환값:
      None: 결과를 콘솔에 출력만 하고 값을 반환하지 않는다.
    """
    # StationConfig: 스테이션 설정을 담는 클래스를 가져온다.
    from Common.Config import StationConfig
    # Station2Inferencer: Station2 전용 하이브리드(YOLO+PatchCore) 추론기를 가져온다.
    from Common.Inferencer import Station2Inferencer

    # Station2에 필요한 설정 객체를 생성한다. Station1보다 설정이 많다 (두 모델을 사용하므로).
    config = StationConfig(
        station_id=2,                        # 스테이션 번호 2 = 조립검사 공정
        model_path=yolo_path,                # YOLO11 모델 파일 경로 (객체 탐지용)
        patchcore_model_path=patchcore_path,  # PatchCore 모델 파일 경로 (표면 이상 탐지용)
        device=device,                       # 추론 장치 (auto/cuda/cpu)
        anomaly_threshold=threshold,         # PatchCore 이상 점수 임계값
        yolo_conf_threshold=0.5,             # YOLO 신뢰도 임계값: 50% 이상 확신하는 탐지만 사용
        yolo_iou_threshold=0.45,             # YOLO IoU 임계값: 겹치는 박스를 제거하는 NMS 기준
        yolo_input_size=640,                 # YOLO 모델에 입력되는 이미지 크기 (640x640 픽셀)
        patchcore_input_size=224,            # PatchCore 모델에 입력되는 이미지 크기 (224x224 픽셀)
    )

    # 설정을 기반으로 Station2 추론기 인스턴스(객체)를 생성한다.
    inferencer = Station2Inferencer(config)
    # YOLO와 PatchCore 두 모델을 모두 메모리에 로드한다.
    inferencer.load_model()

    # 테스트 정보를 보기 좋게 콘솔에 출력한다.
    print(f"\n{'='*50}")
    print(f"  Station2 (조립검사) YOLO11+PatchCore 테스트")
    print(f"{'='*50}")
    print(f"  이미지 크기:     {image.shape}")
    print(f"  YOLO 모델:       {yolo_path}")
    print(f"  PatchCore 모델:  {patchcore_path}")
    print(f"  임계값:          {threshold}")
    print(f"{'='*50}")

    # 추론 시작 전 시각을 기록한다 (소요 시간 측정용).
    t0 = time.perf_counter()
    # 이미지를 YOLO+PatchCore 하이브리드 모델에 넣어 추론을 실행한다.
    result = inferencer.infer(image)
    # 추론 소요 시간을 밀리초 단위로 계산한다.
    elapsed_ms = (time.perf_counter() - t0) * 1000

    # 추론 결과를 콘솔에 출력한다. Station2는 Station1보다 결과 항목이 많다.
    print(f"\n  [결과]")
    # 최종 판정 (OK 또는 NG)
    print(f"  판정:           {result['result']}")
    # 종합 점수 (YOLO 탐지 + PatchCore 이상 점수를 합산한 값)
    print(f"  총점수:         {result['score']}")
    # 발견된 결함 목록. 없으면 빈 리스트이므로 '(없음)'을 출력한다.
    print(f"  결함목록:       {result['defects'] or '(없음)'}")
    # 캡이 정상적으로 장착되었는지 여부 (True/False)
    print(f"  캡 정상:        {result['cap_ok']}")
    # 라벨이 정상적으로 부착되었는지 여부 (True/False)
    print(f"  라벨 정상:      {result['label_ok']}")
    # 충전량(음료 수위)이 정상 범위인지 여부 (True/False)
    print(f"  충전량 정상:    {result['fill_ok']}")
    # PatchCore가 계산한 라벨 표면 이상 점수
    print(f"  PatchCore점수:  {result['patchcore_score']}")
    # YOLO가 탐지한 객체 수 (캡, 라벨, 충전량 등)
    print(f"  YOLO 탐지수:    {len(result['detections'])}")
    # 추론에 걸린 시간 (밀리초)
    print(f"  추론시간:       {elapsed_ms:.1f} ms")

    # YOLO가 탐지한 각 객체의 상세 정보를 순회하며 출력한다.
    # enumerate는 인덱스(i)와 값(det)을 동시에 가져온다.
    for i, det in enumerate(result["detections"]):
        # 각 탐지 결과의 클래스명, 신뢰도, 바운딩박스 좌표, 위치 정상 여부를 출력한다.
        print(f"    탐지 #{i+1}: {det['class']} conf={det['conf']} "
              f"bbox={det['bbox']} position_ok={det['position_ok']}")
    # 빈 줄을 출력하여 가독성을 높인다.
    print()

    # 바운딩 박스가 그려진 오버레이 이미지가 있으면 파일로 저장을 시도한다.
    if result.get("bbox_overlay") is not None:
        try:
            # OpenCV를 가져온다.
            import cv2
            # 바운딩 박스 오버레이 이미지를 JPEG 파일로 저장한다.
            cv2.imwrite("test_station2_overlay.jpg", result["bbox_overlay"])
            print("  오버레이 저장: test_station2_overlay.jpg")
        except ImportError:
            # OpenCV가 설치되지 않으면 저장을 건너뛴다 (필수 기능이 아니므로).
            pass


def main():
    """메인 함수: 커맨드라인 인자를 파싱하고, 해당 스테이션의 추론 테스트를 실행한다.

    목적:
      - 사용자가 터미널에서 입력한 옵션(--station, --image 등)을 분석한다.
      - 옵션에 따라 적절한 이미지를 로드하고 해당 스테이션의 테스트 함수를 호출한다.

    매개변수: 없음 (커맨드라인에서 인자를 받음)
    반환값: 없음
    """
    # ArgumentParser: 커맨드라인 인자를 정의하고 파싱하는 객체를 생성한다.
    # description은 --help 옵션 사용 시 출력되는 설명 문구이다.
    parser = argparse.ArgumentParser(description="AI 추론 테스트")
    # --station: 테스트할 스테이션 번호. required=True이므로 반드시 입력해야 한다.
    # choices=[1, 2]: 1 또는 2만 입력 가능하도록 제한한다.
    parser.add_argument("--station", type=int, required=True, choices=[1, 2],
                        help="테스트할 스테이션 (1=입고, 2=조립)")
    # --image: 테스트에 사용할 이미지 파일의 경로. 선택 사항이다.
    parser.add_argument("--image", type=str, default="",
                        help="테스트 이미지 경로 (.jpg/.png)")
    # --dummy: 이 플래그를 넣으면 랜덤 더미 이미지를 생성해서 테스트한다.
    # action="store_true": 플래그가 있으면 True, 없으면 False가 된다.
    parser.add_argument("--dummy", action="store_true",
                        help="더미 이미지로 테스트 (모델 없이 동작 확인)")
    # --model: 모델 파일 경로를 직접 지정한다. 지정하지 않으면 기본 경로를 사용한다.
    parser.add_argument("--model", type=str, default="",
                        help="모델 파일 경로 (Station1: .ckpt, Station2: .pt)")
    # --patchcore: Station2에서 사용하는 PatchCore 모델 경로를 별도로 지정한다.
    parser.add_argument("--patchcore", type=str, default="",
                        help="Station2 전용: PatchCore 모델 경로 (.ckpt)")
    # --threshold: 불량 판정 기준 점수. 기본값 0.5는 보통 적절한 시작점이다.
    parser.add_argument("--threshold", type=float, default=0.5,
                        help="이상 점수 임계값 (기본: 0.5)")
    # --device: GPU(cuda) 또는 CPU를 선택한다. auto면 GPU 있으면 GPU를 사용한다.
    parser.add_argument("--device", type=str, default="auto",
                        choices=["auto", "cuda", "cpu"],
                        help="추론 디바이스 (기본: auto)")
    # parse_args(): 사용자가 입력한 커맨드라인 인자들을 실제로 파싱하여 args 객체에 저장한다.
    args = parser.parse_args()

    # ── 이미지 로드 단계 ──
    # 어떤 방식으로 이미지를 준비할지 결정한다 (더미 / 파일 / 미지정).

    # --dummy 옵션이 지정된 경우: 랜덤 더미 이미지를 생성한다.
    if args.dummy:
        # create_dummy_image 함수를 호출하여 640x480 크기의 랜덤 이미지를 만든다.
        image = create_dummy_image()
        print("더미 이미지 사용 (640x480 랜덤)")
    # --image 옵션으로 이미지 경로가 지정된 경우: 해당 파일을 읽어온다.
    elif args.image:
        try:
            # OpenCV를 가져온다. 이미지 파일을 읽으려면 반드시 필요하다.
            import cv2
            # cv2.imread: 이미지 파일을 BGR 형식의 numpy 배열로 읽어온다.
            image = cv2.imread(args.image)
            # 파일이 없거나 손상되었으면 imread는 None을 반환한다.
            if image is None:
                print(f"이미지 로드 실패: {args.image}")
                # sys.exit(1): 에러 코드 1로 프로그램을 즉시 종료한다.
                sys.exit(1)
            print(f"이미지 로드: {args.image}")
        except ImportError:
            # OpenCV가 설치되지 않은 경우 안내 메시지를 출력하고 종료한다.
            print("opencv-python이 필요합니다: pip install opencv-python")
            sys.exit(1)
    else:
        # --image도 --dummy도 지정하지 않은 경우: 사용법을 안내하고 종료한다.
        print("--image 또는 --dummy 옵션을 지정하세요.")
        sys.exit(1)

    # ── 테스트 실행 단계 ──
    # 지정된 스테이션 번호에 따라 해당 테스트 함수를 호출한다.

    if args.station == 1:
        # Station1: PatchCore 모델 경로를 결정한다.
        # args.model이 비어있으면 기본 경로를 사용한다. or 연산자는 앞이 빈 문자열(Falsy)이면 뒤의 값을 선택한다.
        model_path = args.model or "./models/station1_patchcore.ckpt"
        # Station1 PatchCore 추론 테스트를 실행한다.
        test_station1(image, model_path, args.threshold, args.device)
    else:
        # Station2: YOLO와 PatchCore 두 모델의 경로를 각각 결정한다.
        yolo_path = args.model or "./models/station2_yolo11.pt"
        patchcore_path = args.patchcore or "./models/station2_patchcore.ckpt"
        # Station2 하이브리드(YOLO+PatchCore) 추론 테스트를 실행한다.
        test_station2(image, yolo_path, patchcore_path, args.threshold, args.device)


# 이 파일이 직접 실행될 때만 main() 함수를 호출한다.
# 왜 필요한가: 다른 파일에서 import할 때는 main()이 자동 실행되지 않도록 하기 위함이다.
# __name__은 직접 실행 시 "__main__", import 시에는 모듈 이름이 된다.
if __name__ == "__main__":
    main()
