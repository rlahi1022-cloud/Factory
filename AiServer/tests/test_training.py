"""test_training.py
학습서버 테스트 스크립트.

왜 필요한가:
  - AI 모델의 학습(training) 파이프라인이 정상적으로 동작하는지 검증한다.
  - PatchCore(이상 탐지 모델)와 YOLO11(객체 탐지 모델) 학습을 각각 테스트할 수 있다.
  - 데이터 증강(augmentation) 기능도 별도로 테스트할 수 있다.

사용법:
  cd AiServer

  # PatchCore 학습 테스트 (Station1 입고검사)
  #   정상 이미지만으로 학습하는 비지도학습 방식이다.
  python tests/test_training.py --type patchcore --station 1 --data data/station1/normal

  # PatchCore 학습 테스트 (Station2 라벨표면)
  #   라벨 표면의 정상 이미지로 학습하여 라벨 결함을 탐지한다.
  python tests/test_training.py --type patchcore --station 2 --data data/station2/patchcore/normal

  # YOLO11 학습 테스트 (Station2 조립검사)
  #   바운딩 박스 라벨이 있는 지도학습 방식으로 캡/라벨/충전량을 탐지한다.
  python tests/test_training.py --type yolo --data data/station2/yolo

  # 데이터 증강만 실행
  #   원본 이미지를 변형(회전, 뒤집기 등)하여 학습 데이터를 늘린다.
  python tests/test_training.py --augment data/station1/normal --factor 5
"""

# argparse: 커맨드라인 인자(--type, --station, --data 등)를 파싱하기 위한 표준 라이브러리
import argparse
# sys: 모듈 검색 경로 추가를 위해 사용하는 시스템 라이브러리
import sys
# Path: 파일/폴더 경로를 안전하게 다루기 위한 객체지향 경로 라이브러리
from pathlib import Path

# AiServer 루트 폴더를 파이썬 모듈 검색 경로에 추가한다.
# 왜: tests 폴더 안에서 실행해도 Training, Common 등 상위 폴더의 모듈을 import할 수 있게 하기 위함이다.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


def test_patchcore_training(station_id: int, data_dir: str, device: str = "auto") -> None:
    """PatchCore 모델 학습을 테스트하는 함수.

    목적:
      - PatchCore는 '정상' 이미지만 학습하는 비지도학습(unsupervised) 이상 탐지 모델이다.
      - 정상 이미지의 특징(feature)을 메모리뱅크에 저장해두고, 추론 시 입력 이미지가
        정상과 얼마나 다른지를 점수로 계산하여 불량을 판별한다.
      - 이 함수는 학습 데이터를 검증하고, 실제 학습을 실행하며, 결과를 출력한다.

    매개변수:
      station_id (int): 스테이션 번호 (1=입고검사, 2=조립검사 라벨 표면).
      data_dir (str): 정상 이미지가 저장된 폴더 경로.

    반환값:
      None: 결과를 콘솔에 출력만 한다.
    """
    # PatchcoreTrainer: PatchCore 학습을 수행하는 트레이너 클래스를 가져온다.
    from Training.TrainPatchcore import PatchcoreTrainer

    # 테스트 시작 정보를 보기 좋게 콘솔에 출력한다.
    print(f"\n{'='*50}")
    print(f"  PatchCore 학습 테스트 (Station {station_id})")
    print(f"{'='*50}")
    print(f"  데이터 경로: {data_dir}")

    # ── 데이터 폴더 존재 여부 확인 ──
    # 학습 전에 데이터가 실제로 존재하는지 먼저 확인한다.
    data_path = Path(data_dir)
    # .exists(): 해당 경로가 실제로 존재하는지 확인한다.
    if not data_path.exists():
        # 폴더가 없으면 에러 메시지와 함께 올바른 폴더 구조를 안내한다.
        print(f"\n  [오류] 데이터 폴더가 없습니다: {data_dir}")
        print(f"  아래 경로에 정상 이미지를 넣어주세요:")
        print(f"    {data_dir}/")
        print(f"    └── *.jpg, *.png, *.bmp (정상 이미지)")
        # return으로 함수를 종료한다 (학습을 진행하지 않는다).
        return

    # ── 이미지 파일 수집 ──
    # glob: 특정 패턴에 맞는 파일을 검색한다. "*.jpg"는 .jpg로 끝나는 모든 파일을 의미한다.
    # 세 가지 이미지 형식(JPG, PNG, BMP)을 모두 수집한다.
    images = list(data_path.glob("*.jpg")) + list(data_path.glob("*.png")) + list(data_path.glob("*.bmp"))
    # augmented(증강된) 이미지가 있는 하위 폴더도 확인한다.
    aug_dir = data_path / "augmented"
    # 증강 폴더가 존재하면 그 안의 이미지도 학습 데이터에 포함한다.
    if aug_dir.exists():
        images += list(aug_dir.glob("*.jpg")) + list(aug_dir.glob("*.png"))

    # 수집한 이미지 수를 출력한다.
    print(f"  이미지 수:   {len(images)}")
    # 이미지가 하나도 없으면 학습을 진행할 수 없으므로 에러를 출력하고 종료한다.
    if len(images) == 0:
        print(f"\n  [오류] 이미지가 없습니다. 정상 이미지를 넣어주세요.")
        return

    # ── 진행률 콜백 함수 정의 ──
    # 콜백(callback): 학습 도중 트레이너가 주기적으로 호출하여 진행 상태를 알려주는 함수이다.
    # 왜 콜백을 사용하는가: 학습이 오래 걸리므로 현재 진행률을 실시간으로 확인하기 위함이다.
    def progress(info):
        # info['progress']: 진행률(0~100), info['status']: 현재 상태 설명 문자열
        # :3d는 3자리 정수로 정렬하여 출력한다 (예: "  5%", " 50%", "100%").
        print(f"  [{info['progress']:3d}%] {info['status']}")

    # ── PatchCore 트레이너 생성 ──
    # 학습에 필요한 모든 설정을 지정하여 트레이너 객체를 생성한다.
    trainer = PatchcoreTrainer(
        station_id=station_id,           # 스테이션 번호 (모델 파일명에 포함됨)
        data_dir=data_dir,               # 정상 이미지 폴더 경로
        output_dir="./models",           # 학습 완료된 모델을 저장할 폴더
        backbone="wide_resnet50_2",      # 특징 추출에 사용할 백본 네트워크 (ImageNet 사전학습 모델)
        input_size=224,                  # 모델 입력 이미지 크기 (224x224 픽셀)
        batch_size=32,                   # 한 번에 처리하는 이미지 수 (GPU 메모리에 따라 조절)
        num_workers=4,                   # 데이터 로딩에 사용할 병렬 프로세스 수
        device=device,                   # 학습 디바이스 (auto/cuda/cpu)
        progress_callback=progress,      # 위에서 정의한 진행률 출력 콜백 함수를 전달한다
    )

    # 학습 시작을 알리는 메시지를 출력한다.
    print(f"\n  학습 시작...")
    # train() 메서드를 호출하여 실제 학습을 실행한다. 결과는 딕셔너리로 반환된다.
    result = trainer.train()

    # ── 학습 결과 출력 ──
    print(f"\n  [결과]")
    # result['success']: 학습 성공 여부 (True/False)
    print(f"  성공:     {result['success']}")
    # result['model_path']: 저장된 모델 파일의 경로
    print(f"  모델경로: {result['model_path']}")
    # result['version']: 모델 버전 정보 (재학습 시 버전이 올라간다)
    print(f"  버전:     {result['version']}")
    # result['accuracy']: 모델의 정확도 지표
    print(f"  정확도:   {result['accuracy']}")
    # result['message']: 학습 완료 메시지 또는 에러 메시지
    print(f"  메시지:   {result['message']}")
    # 빈 줄을 출력하여 가독성을 높인다.
    print()


def test_yolo_training(data_dir: str, device: str = "auto", epochs: int = 5) -> None:
    """YOLO11 모델 학습을 테스트하는 함수.

    목적:
      - YOLO11은 이미지에서 객체(캡, 라벨, 충전량)의 위치와 종류를 탐지하는 지도학습 모델이다.
      - 학습에는 이미지와 해당 이미지의 바운딩 박스 라벨 파일이 쌍으로 필요하다.
      - 이 함수는 YOLO 데이터셋 구조를 검증하고, 학습을 실행하며, 결과를 출력한다.

    매개변수:
      data_dir (str): YOLO 데이터셋 폴더 경로 (images/, labels/ 하위 폴더를 포함해야 한다).

    반환값:
      None: 결과를 콘솔에 출력만 한다.
    """
    # YoloTrainer: YOLO11 학습을 수행하는 트레이너 클래스를 가져온다.
    # create_data_yaml: YOLO 학습에 필요한 data.yaml 설정 파일을 생성하는 유틸 함수이다.
    from Training.TrainYolo import YoloTrainer, create_data_yaml

    # 테스트 시작 정보를 콘솔에 출력한다.
    print(f"\n{'='*50}")
    print(f"  YOLO11 학습 테스트 (Station 2)")
    print(f"{'='*50}")
    print(f"  데이터 경로: {data_dir}")

    # ── YOLO 데이터셋 구조 검증 ──
    # YOLO는 images/train, labels/train 폴더 구조를 요구한다.
    data_path = Path(data_dir)
    # 학습 이미지 폴더 경로
    train_img_dir = data_path / "images" / "train"
    # 학습 라벨 폴더 경로 (각 이미지에 대응하는 .txt 파일)
    train_lbl_dir = data_path / "labels" / "train"

    # 이미지 또는 라벨 폴더가 없으면 올바른 구조를 안내하고 종료한다.
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
        # YOLO 라벨 파일의 형식을 설명한다.
        # 각 줄: class_id center_x center_y width height (모두 0~1 사이로 정규화된 값)
        print(f"\n  라벨 포맷 (각 줄): class_id cx cy w h")
        print(f"    0 = cap, 1 = label, 2 = liquid_level")
        print(f"  예시: 0 0.5 0.1 0.3 0.12")
        return

    # ── 데이터 수량 확인 ──
    # 학습 이미지 파일 수를 세어 출력한다.
    train_images = list(train_img_dir.glob("*.jpg")) + list(train_img_dir.glob("*.png"))
    # 학습 라벨 파일 수를 세어 출력한다. 이미지 수와 같아야 정상이다.
    train_labels = list(train_lbl_dir.glob("*.txt"))
    print(f"  학습 이미지:  {len(train_images)}")
    print(f"  학습 라벨:    {len(train_labels)}")

    # 학습 이미지가 없으면 학습을 진행할 수 없다.
    if len(train_images) == 0:
        print(f"\n  [오류] 학습 이미지가 없습니다.")
        return

    # ── data.yaml 파일 생성 ──
    # YOLO 학습에는 데이터셋 정보를 담은 data.yaml 파일이 필수이다.
    # 이 파일에는 이미지 경로, 클래스 이름, 클래스 수 등이 정의된다.
    data_yaml = str(data_path / "data.yaml")
    # data.yaml이 아직 없으면 자동으로 생성한다.
    if not Path(data_yaml).exists():
        # create_data_yaml: 데이터 폴더 구조를 기반으로 data.yaml을 자동 생성하는 헬퍼 함수
        create_data_yaml(data_dir, data_yaml)
        print(f"  data.yaml 생성: {data_yaml}")

    # ── 진행률 콜백 함수 정의 ──
    # 학습 도중 현재 진행 상태를 출력하는 콜백 함수이다.
    def progress(info):
        # info 딕셔너리에서 진행률(%)과 상태 메시지를 꺼내 출력한다.
        print(f"  [{info['progress']:3d}%] {info['status']}")

    # ── YOLO 트레이너 생성 ──
    # YOLO11 학습에 필요한 모든 설정을 지정하여 트레이너 객체를 생성한다.
    trainer = YoloTrainer(
        data_yaml=data_yaml,             # 데이터셋 정보 파일 경로
        output_dir="./models",           # 학습 완료 모델 저장 폴더
        base_model="yolo11n.pt",         # 사전학습된 YOLO11 Nano 모델 (전이학습 기반 모델)
        input_size=640,                  # YOLO 입력 이미지 크기 (640x640 픽셀)
        epochs=epochs,                   # 전체 데이터셋 반복 횟수 (테스트: 5, 실제: 100)
        batch_size=16,                   # 한 번에 16장씩 묶어서 학습한다
        patience=20,                     # 20 에폭 동안 성능이 안 오르면 학습을 조기 종료(Early Stopping)한다
        device=device,                   # 학습 디바이스 (auto/cuda/cpu)
        progress_callback=progress,      # 진행률 출력 콜백 함수
    )

    # 학습 시작을 알리는 메시지를 출력한다.
    print(f"\n  학습 시작...")
    # train() 메서드를 호출하여 실제 YOLO 학습을 실행한다.
    result = trainer.train()

    # ── 학습 결과 출력 ──
    print(f"\n  [결과]")
    # 학습 성공 여부
    print(f"  성공:     {result['success']}")
    # 저장된 학습 완료 모델 파일 경로
    print(f"  모델경로: {result['model_path']}")
    # 모델 버전 정보
    print(f"  버전:     {result['version']}")
    # 검증 데이터에 대한 정확도 (mAP 등)
    print(f"  정확도:   {result['accuracy']}")
    # 학습 완료 또는 에러 메시지
    print(f"  메시지:   {result['message']}")
    # 빈 줄을 출력하여 가독성을 높인다.
    print()


def test_augmentation(data_dir: str, factor: int) -> None:
    """데이터 증강(augmentation) 기능을 테스트하는 함수.

    목적:
      - 데이터 증강이란 원본 이미지를 회전, 뒤집기, 밝기 조절 등으로 변형하여
        학습 데이터의 양을 인위적으로 늘리는 기법이다.
      - 딥러닝 모델은 데이터가 많을수록 성능이 좋아지므로, 원본이 적을 때 증강이 필수적이다.
      - 이 함수는 증강을 실행하고, 원본/증강 이미지 수를 비교하여 결과를 확인한다.

    매개변수:
      data_dir (str): 원본 이미지가 있는 폴더 경로.
      factor (int): 증강 배수. 예를 들어 factor=5이면 원본 1장당 5장의 변형 이미지를 생성한다.

    반환값:
      None: 결과를 콘솔에 출력만 한다.
    """
    # augment_dataset: 이미지 폴더를 입력받아 증강된 이미지를 생성하는 함수를 가져온다.
    from Training.TrainPatchcore import augment_dataset

    # 테스트 시작 정보를 콘솔에 출력한다.
    print(f"\n{'='*50}")
    print(f"  데이터 증강 테스트")
    print(f"{'='*50}")
    print(f"  데이터 경로: {data_dir}")
    # 몇 배로 증강할 것인지 출력한다. 예: "5x"는 원본의 5배만큼 생성한다는 뜻이다.
    print(f"  증강 배수:   {factor}x")

    # 원본 이미지 파일을 수집한다 (JPG, PNG, BMP 형식).
    data_path = Path(data_dir)
    images = list(data_path.glob("*.jpg")) + list(data_path.glob("*.png")) + list(data_path.glob("*.bmp"))
    # 원본 이미지 수를 출력한다.
    print(f"  원본 이미지: {len(images)}")

    # 원본 이미지가 없으면 증강할 수 없으므로 에러를 출력하고 종료한다.
    if len(images) == 0:
        print(f"\n  [오류] 이미지가 없습니다.")
        return

    # augment_dataset 함수를 호출하여 실제 증강을 실행한다.
    # 증강된 이미지는 data_dir/augmented/ 하위 폴더에 저장된다.
    augment_dataset(data_dir, factor=factor)

    # 증강된 이미지 수를 확인한다.
    aug_dir = data_path / "augmented"
    # 증강 폴더에서 생성된 이미지 파일들을 수집한다.
    aug_images = list(aug_dir.glob("*.jpg")) + list(aug_dir.glob("*.png"))
    # 증강된 이미지 수와 저장 위치를 출력한다.
    print(f"  증강 이미지: {len(aug_images)} (in {aug_dir})")
    # 빈 줄을 출력하여 가독성을 높인다.
    print()


def main():
    """메인 함수: 커맨드라인 인자를 파싱하고, 지정된 테스트를 실행한다.

    목적:
      - 사용자가 터미널에서 입력한 옵션에 따라 적절한 테스트 함수를 호출한다.
      - --type 옵션으로 학습 유형(patchcore/yolo)을 선택할 수 있다.
      - --augment 옵션으로 데이터 증강만 따로 실행할 수 있다.

    매개변수: 없음 (커맨드라인에서 인자를 받음)
    반환값: 없음
    """
    # ArgumentParser: 커맨드라인 인자를 정의하고 파싱하는 객체를 생성한다.
    parser = argparse.ArgumentParser(description="AI 학습 테스트")
    # --type: 학습 유형을 선택한다. patchcore(이상 탐지) 또는 yolo(객체 탐지).
    parser.add_argument("--type", choices=["patchcore", "yolo"],
                        help="학습 유형")
    # --station: PatchCore 학습 시 스테이션 번호를 지정한다. 기본값 1 (입고검사).
    parser.add_argument("--station", type=int, default=1, choices=[1, 2],
                        help="스테이션 번호 (PatchCore용)")
    # --data: 학습 데이터 폴더 경로를 지정한다. 지정하지 않으면 기본 경로를 사용한다.
    parser.add_argument("--data", type=str, default="",
                        help="데이터 경로")
    # --augment: 증강할 데이터 폴더를 지정한다. 이 옵션이 있으면 증강만 실행하고 학습은 하지 않는다.
    parser.add_argument("--augment", type=str, default="",
                        help="증강할 데이터 폴더 경로")
    # --factor: 증강 배수를 지정한다. 기본값 5배.
    parser.add_argument("--factor", type=int, default=5,
                        help="증강 배수 (기본: 5)")
    # --device: 학습에 사용할 디바이스를 지정한다.
    #   "auto": GPU 있으면 GPU, 없으면 CPU (기본값)
    #   "cpu": CPU 강제 사용 (GPU 드라이버 문제 시 유용)
    #   "cuda": GPU 강제 사용
    parser.add_argument("--device", type=str, default="auto",
                        choices=["auto", "cuda", "cpu"],
                        help="학습 디바이스 (기본: auto)")
    # --epochs: YOLO 학습 에폭 수. 테스트 시에는 5 정도로 줄여서 빠르게 확인할 수 있다.
    parser.add_argument("--epochs", type=int, default=5,
                        help="YOLO 학습 에폭 수 (기본: 5, 실제 운영: 100)")
    args = parser.parse_args()

    # ── 실행할 테스트 결정 ──
    # 우선순위: 증강(--augment) > PatchCore(--type patchcore) > YOLO(--type yolo)

    if args.augment:
        # --augment 옵션이 지정되면 데이터 증강만 실행한다.
        test_augmentation(args.augment, args.factor)
    elif args.type == "patchcore":
        # --type이 patchcore이면 PatchCore 학습 테스트를 실행한다.
        # args.data가 비어있으면 스테이션 번호에 맞는 기본 경로를 사용한다.
        # or 연산자: 앞의 값이 빈 문자열(Falsy)이면 뒤의 값을 선택한다.
        # Station1: data/station1/normal, Station2: data/station2/patchcore/normal
        if args.station == 1:
            data = args.data or "./data/station1/normal"
        else:
            data = args.data or "./data/station2/patchcore/normal"
        test_patchcore_training(args.station, data, args.device)
    elif args.type == "yolo":
        # --type이 yolo이면 YOLO11 학습 테스트를 실행한다.
        data = args.data or "./data/station2/yolo"
        test_yolo_training(data, args.device, args.epochs)
    else:
        # 아무 옵션도 지정하지 않으면 도움말을 출력한다.
        parser.print_help()


# 이 파일이 직접 실행될 때만 main() 함수를 호출한다.
# 다른 파일에서 import할 때는 main()이 자동 실행되지 않는다.
if __name__ == "__main__":
    main()
