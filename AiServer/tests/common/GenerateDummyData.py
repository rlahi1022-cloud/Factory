"""generate_dummy_data.py
테스트용 더미(가짜) 학습 데이터를 생성하는 스크립트.

왜 필요한가:
  - 실제 공장 이미지가 없을 때도 학습/추론 파이프라인을 테스트하기 위해 가짜 데이터를 만든다.
  - OpenCV로 페트병 형태의 시뮬레이션 이미지를 그려서 실제 데이터와 비슷한 구조를 갖추게 한다.
  - Station1(입고검사)과 Station2(조립검사) 모두의 학습 데이터를 자동 생성한다.

사용법:
  cd AiServer
  python tests/generate_dummy_data.py

생성 결과:
  data/station1/normal/       -- 빈 용기 정상 이미지 20장
  data/station1/test/         -- 테스트 이미지 5장 (일부 결함 시뮬레이션)
  data/station2/yolo/         -- YOLO 포맷 데이터셋 (이미지 + 라벨)
  data/station2/patchcore/normal/ -- 라벨 표면 정상 이미지 20장
  data/station2/test/         -- 조립 테스트 이미지 5장
"""

# os: 운영체제 관련 기능 (파일 경로, 환경 변수 등)을 제공하는 표준 라이브러리
import os
# random: 랜덤 값 생성 (결함 위치, 밝기 변화 등에 사용)을 위한 표준 라이브러리
import random
# Path: 운영체제에 독립적인 경로 처리를 위한 객체지향 라이브러리
from pathlib import Path

# cv2 (OpenCV): 이미지를 읽고, 그리고, 저장하는 컴퓨터 비전 핵심 라이브러리
# 이 스크립트에서는 도형 그리기(원, 사각형, 선 등)와 이미지 저장에 사용한다.
import cv2
# numpy: 이미지 데이터(다차원 배열)를 생성하고 처리하는 핵심 수학 라이브러리
import numpy as np


def make_dirs():
    """학습/테스트 데이터를 저장할 폴더 구조를 생성하는 함수.

    목적:
      - 데이터를 저장하기 전에 필요한 모든 폴더를 미리 만들어둔다.
      - 이미 폴더가 있어도 에러가 나지 않도록 exist_ok=True 옵션을 사용한다.

    매개변수: 없음
    반환값: 없음
    """
    # 생성해야 할 모든 폴더 경로를 리스트로 정의한다.
    dirs = [
        "data/station1/normal",                # Station1 정상 이미지 (PatchCore 학습용)
        "data/station1/test",                   # Station1 테스트 이미지 (정상+불량)
        "data/station2/yolo/images/train",      # Station2 YOLO 학습 이미지
        "data/station2/yolo/images/val",        # Station2 YOLO 검증 이미지
        "data/station2/yolo/labels/train",      # Station2 YOLO 학습 라벨 (바운딩 박스 좌표)
        "data/station2/yolo/labels/val",        # Station2 YOLO 검증 라벨
        "data/station2/patchcore/normal",       # Station2 라벨 표면 정상 이미지 (PatchCore 학습용)
        "data/station2/test",                   # Station2 테스트 이미지 (정상+불량)
    ]
    # 각 폴더 경로를 순회하며 폴더를 생성한다.
    for d in dirs:
        # parents=True: 중간 폴더(data, station1 등)가 없어도 자동으로 함께 생성한다.
        # exist_ok=True: 이미 폴더가 존재해도 에러를 발생시키지 않는다.
        Path(d).mkdir(parents=True, exist_ok=True)


# ── Station1: 빈 용기 이미지 생성 ──
# Station1은 입고검사 공정으로, 빈 페트병의 외관 결함(스크래치, 오염, 균열)을 검사한다.

def draw_empty_bottle(w=224, h=224, defect=False):
    """빈 페트병 시뮬레이션 이미지를 그리는 함수.

    목적:
      - OpenCV 도형 그리기 함수를 사용해 페트병 형태의 가짜 이미지를 생성한다.
      - defect=True이면 스크래치, 오염, 균열 중 하나를 랜덤으로 추가한다.
      - 실제 이미지를 대신하여 모델 학습/추론 파이프라인을 테스트하는 데 사용한다.

    매개변수:
      w (int): 이미지 가로 크기 (픽셀). 기본값 224 (PatchCore 입력 크기).
      h (int): 이미지 세로 크기 (픽셀). 기본값 224.
      defect (bool): True이면 결함을 추가한다. False이면 정상 이미지를 생성한다.

    반환값:
      np.ndarray: BGR 형식의 시뮬레이션 이미지 (shape: (h, w, 3)).
    """
    # 밝은 회색(220) 배경의 빈 이미지를 생성한다.
    # np.ones: 모든 값이 1인 배열을 만들고, *220으로 밝기 220의 배경을 만든다.
    # dtype=np.uint8: 픽셀값은 0~255 범위의 부호 없는 8비트 정수이다.
    img = np.ones((h, w, 3), dtype=np.uint8) * 220

    # ── 병 몸체 그리기 (투명한 느낌의 타원) ──
    # 이미지 중심 좌표를 계산한다.
    cx, cy = w // 2, h // 2
    # cv2.ellipse: 타원을 그린다. (중심, 축 크기, 회전각, 시작각, 끝각, 색, 두께)
    # 두께 -1은 내부를 채우라는 의미이다 (filled).
    cv2.ellipse(img, (cx, cy), (w // 4, h // 3), 0, 0, 360, (200, 210, 220), -1)
    # 같은 위치에 두께 2의 테두리를 그려 병 윤곽선을 표현한다.
    cv2.ellipse(img, (cx, cy), (w // 4, h // 3), 0, 0, 360, (180, 190, 200), 2)

    # ── 병목 그리기 (좁은 사각형) ──
    # 병목의 좌우 폭을 이미지 너비의 1/8로 설정한다.
    neck_w = w // 8
    # cv2.rectangle: 사각형을 그린다. (이미지, 좌상단 좌표, 우하단 좌표, 색, 두께)
    # 두께 -1로 내부를 채운다.
    cv2.rectangle(img, (cx - neck_w, 20), (cx + neck_w, cy - h // 3), (190, 200, 210), -1)
    # 병목 테두리를 그린다.
    cv2.rectangle(img, (cx - neck_w, 20), (cx + neck_w, cy - h // 3), (170, 180, 190), 2)

    # ── 병 바닥 그리기 (타원) ──
    # 바닥 부분을 타원으로 표현한다.
    cv2.ellipse(img, (cx, cy + h // 3), (w // 4, h // 10), 0, 0, 360, (180, 190, 200), -1)

    # ── 반사광 효과 (수직선) ──
    # 병에 빛이 반사되는 느낌을 주기 위해 밝은 세로 선을 그린다.
    cv2.line(img, (cx - w // 6, cy - h // 5), (cx - w // 6, cy + h // 5), (240, 245, 250), 2)

    # ── 결함 추가 (defect=True인 경우) ──
    if defect:
        # 3가지 결함 유형 중 하나를 랜덤으로 선택한다.
        defect_type = random.choice(["scratch", "contamination", "crack"])
        if defect_type == "scratch":
            # 스크래치: 랜덤 위치에 어두운 선을 그린다.
            # 시작점의 x, y 좌표를 병 몸체 범위 내에서 랜덤으로 결정한다.
            x1 = random.randint(cx - w // 4, cx + w // 4)
            y1 = random.randint(cy - h // 4, cy + h // 4)
            # 끝점은 시작점에서 약간 떨어진 위치로 설정한다.
            x2 = x1 + random.randint(-30, 30)
            y2 = y1 + random.randint(20, 50)
            # 어두운 색(50, 50, 50)으로 선을 그려 스크래치를 표현한다.
            cv2.line(img, (x1, y1), (x2, y2), (50, 50, 50), 2)
        elif defect_type == "contamination":
            # 오염: 랜덤 위치에 어두운 원을 그린다.
            # 오염 중심점 좌표를 병 몸체 범위 내에서 랜덤으로 결정한다.
            px = random.randint(cx - w // 5, cx + w // 5)
            py = random.randint(cy - h // 5, cy + h // 5)
            # 오염 크기(반지름)를 5~15 픽셀 범위에서 랜덤으로 결정한다.
            r = random.randint(5, 15)
            # 갈색 계열의 원을 그려 오염을 표현한다. 두께 -1은 내부를 채운다.
            cv2.circle(img, (px, py), r, (80, 60, 40), -1)
        elif defect_type == "crack":
            # 균열: 여러 점을 연결한 불규칙한 선(폴리라인)을 그린다.
            pts = []
            # 균열 시작점을 병 몸체 범위 내에서 랜덤으로 결정한다.
            x = random.randint(cx - w // 5, cx + w // 5)
            y = random.randint(cy - h // 5, cy)
            # 5개의 점을 순차적으로 생성하여 균열 경로를 만든다.
            for _ in range(5):
                pts.append([x, y])
                # 다음 점은 약간 좌우로 흔들리면서 아래로 내려간다 (자연스러운 균열 표현).
                x += random.randint(-10, 10)
                y += random.randint(5, 15)
            # cv2.polylines: 여러 점을 연결하는 선을 그린다.
            # False: 마지막 점과 첫 점을 연결하지 않는다 (열린 도형).
            cv2.polylines(img, [np.array(pts)], False, (30, 30, 30), 2)

    # ── 자연스러운 노이즈 추가 ──
    # 실제 카메라로 촬영한 이미지에는 약간의 노이즈가 있으므로, 가우시안 노이즈를 추가한다.
    # np.random.normal: 평균 0, 표준편차 3의 가우시안(정규분포) 난수를 생성한다.
    noise = np.random.normal(0, 3, img.shape).astype(np.float32)
    # np.clip: 노이즈를 더한 후 픽셀값이 0~255 범위를 벗어나지 않도록 잘라낸다(클리핑).
    img = np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)
    # 완성된 시뮬레이션 이미지를 반환한다.
    return img


def generate_station1_data():
    """Station1 (입고검사) 더미 데이터를 생성하는 함수.

    목적:
      - PatchCore 학습용 정상 이미지 20장과 테스트용 이미지 5장을 생성한다.
      - 정상 이미지에는 밝기/회전 변형을 랜덤으로 적용하여 다양성을 준다.
      - 테스트 이미지에는 정상 2장과 결함 3장을 포함한다.

    매개변수: 없음
    반환값: 없음 (파일을 디스크에 저장한다)
    """
    print("Station1 (입고검사) 더미 데이터 생성...")

    # ── 정상 이미지 20장 생성 ──
    # PatchCore 학습에는 정상 이미지만 필요하다 (비지도학습).
    for i in range(20):
        # 결함 없는 빈 병 이미지를 생성한다 (defect=False가 기본값).
        img = draw_empty_bottle(224, 224, defect=False)
        # 랜덤 밝기 변형을 적용하여 이미지 다양성을 높인다.
        # alpha: 대비(contrast) 조절 계수. 0.9~1.1 범위에서 랜덤 (10% 범위의 변화).
        alpha = random.uniform(0.9, 1.1)
        # beta: 밝기(brightness) 조절값. -10~10 범위에서 랜덤.
        beta = random.randint(-10, 10)
        # cv2.convertScaleAbs: 이미지에 대비/밝기 변환을 적용한다. 결과 = alpha * 원본 + beta
        img = cv2.convertScaleAbs(img, alpha=alpha, beta=beta)
        # 정상 이미지를 파일로 저장한다. :03d는 3자리 숫자로 포맷 (000, 001, ..., 019).
        cv2.imwrite(f"data/station1/normal/dummy_normal_{i:03d}.jpg", img)

    # ── 테스트 이미지 5장 생성 (정상 2장 + 결함 3장) ──
    # 정상(OK) 테스트 이미지 2장을 생성한다.
    for i in range(2):
        img = draw_empty_bottle(224, 224, defect=False)
        # 파일명에 "ok"를 포함하여 정상 이미지임을 알 수 있게 한다.
        cv2.imwrite(f"data/station1/test/dummy_ok_{i:03d}.jpg", img)
    # 불량(NG) 테스트 이미지 3장을 생성한다.
    for i in range(3):
        # defect=True로 결함을 추가한다.
        img = draw_empty_bottle(224, 224, defect=True)
        # 파일명에 "ng"를 포함하여 불량 이미지임을 알 수 있게 한다.
        cv2.imwrite(f"data/station1/test/dummy_ng_{i:03d}.jpg", img)

    # 생성 결과를 요약 출력한다.
    print(f"  normal: 20장, test: 5장")


# ── Station2: 조립 완성품 이미지 생성 ──
# Station2는 조립검사 공정으로, 캡/라벨/충전량이 있는 완성된 음료병을 검사한다.

def draw_assembled_bottle(w=640, h=640, missing=None):
    """캡+라벨+충전량이 있는 조립 완성품 시뮬레이션 이미지를 그리는 함수.

    목적:
      - YOLO 객체 탐지 학습에 사용할 조립 완성품 이미지를 생성한다.
      - 각 부품(캡, 라벨, 충전량)의 바운딩 박스 좌표도 함께 반환한다.
      - missing 옵션으로 특정 부품이 없는 불량 이미지를 만들 수 있다.

    매개변수:
      w (int): 이미지 가로 크기 (픽셀). 기본값 640 (YOLO 입력 크기).
      h (int): 이미지 세로 크기 (픽셀). 기본값 640.
      missing (str 또는 None): 빠뜨릴 부품 이름. "cap", "label", "liquid_level" 중 하나.
                               None이면 모든 부품이 있는 정상 이미지를 생성한다.

    반환값:
      tuple: (이미지 배열, 바운딩 박스 딕셔너리)
        - 이미지: BGR 형식의 numpy 배열 (shape: (h, w, 3))
        - boxes: {부품이름: (x1, y1, x2, y2)} 형태의 딕셔너리. 좌상단/우하단 좌표.
    """
    # 밝은 회색(200) 배경 이미지를 생성한다.
    img = np.ones((h, w, 3), dtype=np.uint8) * 200

    # ── 병 몸체 그리기 ──
    # 병 몸체의 중심 x 좌표
    cx = w // 2
    # 병 몸체의 상단/하단 y 좌표
    body_top = 120
    body_bot = 580
    # 병 몸체의 좌/우 x 좌표
    body_left = cx - 100
    body_right = cx + 100
    # 병 몸체를 사각형으로 그린다 (채우기).
    cv2.rectangle(img, (body_left, body_top), (body_right, body_bot), (210, 220, 230), -1)
    # 병 몸체 테두리를 그린다.
    cv2.rectangle(img, (body_left, body_top), (body_right, body_bot), (180, 190, 200), 2)

    # ── 병목 그리기 ──
    # 병목의 좌/우 x 좌표 (몸체보다 좁다)
    neck_l = cx - 30
    neck_r = cx + 30
    # 병목을 사각형으로 그린다 (채우기).
    cv2.rectangle(img, (neck_l, 50), (neck_r, body_top), (200, 210, 220), -1)
    # 병목 테두리를 그린다.
    cv2.rectangle(img, (neck_l, 50), (neck_r, body_top), (170, 180, 190), 2)

    # 각 부품의 바운딩 박스 좌표를 저장할 딕셔너리.
    # 왜: YOLO 라벨 파일을 생성할 때 이 좌표를 정규화하여 사용한다.
    boxes = {}

    # ── 캡 그리기 (파란색) ──
    # missing이 "cap"이 아닐 때만 캡을 그린다 (불량 시뮬레이션용).
    if missing != "cap":
        # 캡의 상단/하단 y 좌표
        cap_y1, cap_y2 = 30, 70
        # 캡의 좌/우 x 좌표
        cap_x1, cap_x2 = cx - 35, cx + 35
        # 파란색 캡을 표현한다 (BGR 형식이므로 B=180, G=50, R=50).
        color = (180, 50, 50)
        # 캡을 사각형으로 그린다 (채우기).
        cv2.rectangle(img, (cap_x1, cap_y1), (cap_x2, cap_y2), color, -1)
        # 캡 테두리를 그린다.
        cv2.rectangle(img, (cap_x1, cap_y1), (cap_x2, cap_y2), (150, 30, 30), 2)
        # 캡의 바운딩 박스 좌표를 딕셔너리에 저장한다.
        boxes["cap"] = (cap_x1, cap_y1, cap_x2, cap_y2)

    # ── 라벨 그리기 (초록/흰 라벨) ──
    # missing이 "label"이 아닐 때만 라벨을 그린다.
    if missing != "label":
        # 라벨의 상단/하단 y 좌표
        label_y1, label_y2 = 200, 420
        # 라벨의 좌/우 x 좌표 (병 몸체보다 약간 안쪽)
        label_x1, label_x2 = body_left + 5, body_right - 5
        # 흰색 라벨 배경을 그린다.
        cv2.rectangle(img, (label_x1, label_y1), (label_x2, label_y2), (240, 240, 240), -1)
        # 초록색 라벨 테두리를 그린다.
        cv2.rectangle(img, (label_x1, label_y1), (label_x2, label_y2), (100, 150, 100), 2)
        # 라벨 위에 "WATER" 텍스트를 넣어 실제 라벨처럼 보이게 한다.
        # cv2.putText: (이미지, 텍스트, 좌하단 좌표, 폰트, 크기, 색, 두께)
        cv2.putText(img, "WATER", (label_x1 + 20, label_y1 + 80),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.5, (60, 120, 60), 3)
        # "500ml" 용량 표시도 추가한다.
        cv2.putText(img, "500ml", (label_x1 + 40, label_y1 + 140),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (100, 100, 100), 2)
        # 라벨의 바운딩 박스 좌표를 딕셔너리에 저장한다.
        boxes["label"] = (label_x1, label_y1, label_x2, label_y2)

    # ── 충전량(수면선) 그리기 ──
    # missing이 "liquid_level"이 아닐 때만 충전량을 그린다.
    if missing != "liquid_level":
        # 충전량 영역의 상단/하단 y 좌표
        fill_y1, fill_y2 = 430, 530
        # 충전량 영역의 좌/우 x 좌표 (병 몸체보다 약간 안쪽)
        fill_x1, fill_x2 = body_left + 10, body_right - 10
        # 충전량 영역을 베이지색으로 채운다 (음료 색상 시뮬레이션).
        cv2.rectangle(img, (fill_x1, fill_y1), (fill_x2, fill_y2), (220, 200, 180), -1)
        # 수면(액체 상단) 라인을 그린다. 수면선은 충전량 검사의 핵심 포인트이다.
        cv2.line(img, (fill_x1, fill_y1), (fill_x2, fill_y1), (150, 130, 110), 2)
        # 충전량의 바운딩 박스 좌표를 딕셔너리에 저장한다.
        boxes["liquid_level"] = (fill_x1, fill_y1, fill_x2, fill_y2)

    # ── 자연스러운 노이즈 추가 ──
    # 실제 카메라 이미지처럼 보이게 가우시안 노이즈를 추가한다.
    noise = np.random.normal(0, 3, img.shape).astype(np.float32)
    # 노이즈를 더한 후 0~255 범위로 클리핑한다.
    img = np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)

    # 완성된 이미지와 바운딩 박스 좌표를 튜플로 반환한다.
    return img, boxes


def box_to_yolo(box, img_w, img_h):
    """절대 좌표 바운딩 박스를 YOLO 정규화 형식으로 변환하는 함수.

    목적:
      - YOLO는 바운딩 박스를 (center_x, center_y, width, height) 형식으로 사용하며,
        모든 값은 이미지 크기로 나눈 0~1 범위의 정규화된 값이어야 한다.
      - 이 함수는 (x1, y1, x2, y2) 절대 좌표를 YOLO 형식으로 변환한다.

    매개변수:
      box (tuple): (x1, y1, x2, y2) 절대 픽셀 좌표. 좌상단과 우하단 모서리.
      img_w (int): 이미지 가로 크기 (정규화 분모).
      img_h (int): 이미지 세로 크기 (정규화 분모).

    반환값:
      tuple: (cx, cy, bw, bh) YOLO 정규화 좌표.
        - cx: 박스 중심의 x 좌표 (0~1)
        - cy: 박스 중심의 y 좌표 (0~1)
        - bw: 박스 너비 (0~1)
        - bh: 박스 높이 (0~1)
    """
    # 절대 좌표를 개별 변수로 분리한다.
    x1, y1, x2, y2 = box
    # 박스 중심 x 좌표를 계산하고, 이미지 너비로 나누어 0~1로 정규화한다.
    cx = ((x1 + x2) / 2) / img_w
    # 박스 중심 y 좌표를 계산하고, 이미지 높이로 나누어 0~1로 정규화한다.
    cy = ((y1 + y2) / 2) / img_h
    # 박스 너비를 이미지 너비로 나누어 정규화한다.
    bw = (x2 - x1) / img_w
    # 박스 높이를 이미지 높이로 나누어 정규화한다.
    bh = (y2 - y1) / img_h
    # YOLO 형식의 정규화된 좌표를 반환한다.
    return cx, cy, bw, bh


# YOLO 라벨에서 사용하는 클래스 ID 매핑 딕셔너리.
# 왜 딕셔너리로 정의하는가: 클래스 이름으로 쉽게 ID를 조회할 수 있도록 하기 위함이다.
# 0=cap(캡), 1=label(라벨), 2=liquid_level(충전량/수면선)
CLASS_MAP = {"cap": 0, "label": 1, "liquid_level": 2}


def generate_station2_data():
    """Station2 (조립검사) 더미 데이터를 생성하는 함수.

    목적:
      - YOLO 학습용 데이터: 정상 이미지 + YOLO 라벨 파일 (train 15장 + val 5장)
      - PatchCore 학습용 데이터: 라벨 표면 정상 crop 이미지 20장
      - 테스트용 데이터: 정상 2장 + 불량(부품 누락) 3장
      - data.yaml: YOLO 학습에 필요한 데이터셋 설정 파일

    매개변수: 없음
    반환값: 없음 (파일을 디스크에 저장한다)
    """
    print("Station2 (조립검사) 더미 데이터 생성...")

    # 이미지 크기를 변수로 정의한다 (YOLO 표준 입력 크기: 640x640).
    img_w, img_h = 640, 640

    # ── YOLO 학습 데이터 생성: train 15장 + val 5장 ──
    # train(학습 데이터): 모델이 학습하는 데 사용하는 이미지
    # val(검증 데이터): 학습 중 모델의 성능을 평가하는 데 사용하는 이미지
    for split, count in [("train", 15), ("val", 5)]:
        for i in range(count):
            # 정상 조립 완성품 이미지와 바운딩 박스를 생성한다.
            img, boxes = draw_assembled_bottle(img_w, img_h)
            # 랜덤 밝기 변형을 적용하여 이미지 다양성을 높인다.
            alpha = random.uniform(0.9, 1.1)
            img = cv2.convertScaleAbs(img, alpha=alpha, beta=random.randint(-5, 5))

            # 이미지를 YOLO 폴더 구조에 맞게 저장한다.
            cv2.imwrite(f"data/station2/yolo/images/{split}/bottle_{i:03d}.jpg", img)

            # ── YOLO 라벨 파일 생성 ──
            # 각 이미지에 대응하는 .txt 라벨 파일을 생성한다.
            # 이미지 파일명과 라벨 파일명이 같아야 YOLO가 매칭할 수 있다.
            with open(f"data/station2/yolo/labels/{split}/bottle_{i:03d}.txt", "w") as f:
                # 각 부품(캡, 라벨, 충전량)의 바운딩 박스를 라벨 파일에 기록한다.
                for cls_name, box in boxes.items():
                    # 클래스 이름을 숫자 ID로 변환한다 (YOLO는 숫자 ID를 사용한다).
                    cls_id = CLASS_MAP[cls_name]
                    # 절대 좌표를 YOLO 정규화 좌표로 변환한다.
                    cx, cy, bw, bh = box_to_yolo(box, img_w, img_h)
                    # YOLO 라벨 형식: "클래스ID 중심x 중심y 너비 높이" (소수점 6자리)
                    f.write(f"{cls_id} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}\n")

    # ── PatchCore 학습용 라벨 표면 정상 crop 이미지 20장 생성 ──
    # 왜 crop하는가: PatchCore는 라벨 표면만 보고 이상을 탐지하므로, 라벨 영역만 잘라낸다.
    for i in range(20):
        # 정상 완성품 이미지를 생성한다.
        img, boxes = draw_assembled_bottle(img_w, img_h)
        # 라벨이 있는 경우에만 crop한다.
        if "label" in boxes:
            # 라벨 바운딩 박스의 좌상단/우하단 좌표를 가져온다.
            x1, y1, x2, y2 = boxes["label"]
            # numpy 배열 슬라이싱으로 라벨 영역만 잘라낸다 (crop).
            # img[y1:y2, x1:x2]는 y1~y2 행, x1~x2 열을 선택한다.
            label_crop = img[y1:y2, x1:x2]
            # PatchCore 입력 크기(224x224)로 리사이즈한다.
            label_crop = cv2.resize(label_crop, (224, 224))
            # 라벨 crop 이미지를 파일로 저장한다.
            cv2.imwrite(f"data/station2/patchcore/normal/label_normal_{i:03d}.jpg", label_crop)

    # ── 테스트 이미지 생성: 정상 2장 + 불량 3장 ──
    # 정상(OK) 테스트 이미지 2장
    for i in range(2):
        # 모든 부품이 있는 정상 이미지를 생성한다.
        img, _ = draw_assembled_bottle(img_w, img_h)
        cv2.imwrite(f"data/station2/test/dummy_ok_{i:03d}.jpg", img)

    # 불량(NG) 테스트 이미지 3장: 각각 캡/라벨/충전량이 하나씩 빠진 이미지
    # enumerate: 인덱스(i)와 값(missing)을 동시에 가져온다.
    for i, missing in enumerate(["cap", "label", "liquid_level"]):
        # missing 파라미터로 해당 부품을 제외한 이미지를 생성한다.
        img, _ = draw_assembled_bottle(img_w, img_h, missing=missing)
        # 파일명에 빠진 부품 이름을 포함하여 어떤 불량인지 알 수 있게 한다.
        cv2.imwrite(f"data/station2/test/dummy_ng_{missing}_{i:03d}.jpg", img)

    # ── data.yaml 생성 ──
    # YOLO 학습 시 데이터셋의 경로, 클래스 이름, 클래스 수를 정의하는 설정 파일이다.
    # 이 파일이 없으면 YOLO 학습을 시작할 수 없다.
    data_yaml = Path("data/station2/yolo/data.yaml")
    # write_text: 파일에 텍스트를 작성한다. YAML 형식으로 데이터셋 정보를 기록한다.
    data_yaml.write_text(f"""path: {Path('data/station2/yolo').resolve()}
train: images/train
val: images/val

names:
  0: cap
  1: label
  2: liquid_level

nc: 3
""", encoding="utf-8")

    # 생성 결과를 요약 출력한다.
    print(f"  YOLO: train 15장 + val 5장 (라벨 포함)")
    print(f"  PatchCore: 라벨 정상 crop 20장")
    print(f"  test: 정상 2장 + 불량 3장")


def main():
    """메인 함수: 모든 더미 데이터를 생성한다.

    목적:
      - 필요한 폴더 구조를 먼저 만들고, Station1과 Station2의 더미 데이터를 순서대로 생성한다.
      - 기존에 있던 실제 사진 파일은 덮어쓰지 않고 유지된다.

    매개변수: 없음
    반환값: 없음
    """
    # 데이터 저장에 필요한 모든 폴더를 생성한다 (이미 있으면 건너뛴다).
    make_dirs()
    # Station1 (입고검사) 더미 데이터를 생성한다.
    generate_station1_data()
    # Station2 (조립검사) 더미 데이터를 생성한다.
    generate_station2_data()
    # 완료 메시지를 출력한다.
    print("\n더미 데이터 생성 완료!")
    # 기존 실제 사진은 파일명이 다르므로 덮어쓰이지 않음을 안내한다.
    print("기존 실제 사진 (20260415_*.jpg)은 그대로 유지됩니다.")


# 이 파일이 직접 실행될 때만 main() 함수를 호출한다.
# 다른 파일에서 import할 때는 개별 함수만 사용할 수 있다.
if __name__ == "__main__":
    main()
