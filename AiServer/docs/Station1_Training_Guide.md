# Station1 (입고검사) 학습 가이드

본 문서는 **입고검사 공정**에 사용되는 PatchCore 이상탐지 모델의
학습부터 테스트까지 전체 흐름을 설명한다.

---

## 1. 전체 개요

### 1-1. 입고검사란?
- **2% 음료 페트병** (빈 병, 캡/라벨 없음)이 조립라인에 **투입되기 전** 외관을 검사하는 공정
- 검사 대상: 빈 2% 페트병
- 검출 불량 유형: 보드마카 표식, 이물질(흰/유색 종이), 스크래치
- 판정: OK (양품) / NG (불량)

### 1-2. 왜 PatchCore인가?
- **정상 이미지만으로 학습** (비지도학습, unsupervised)
- 불량 샘플이 적거나 다양할 때 유리 (모든 불량 유형을 수집할 필요 없음)
- 학습 중 "정상 이미지의 특징을 메모리뱅크에 저장"
- 추론 시 "정상 패턴과 얼마나 다른가"를 anomaly score로 계산

### 1-3. 데이터 흐름
```
카메라 → 이미지 → 전처리(224x224) → PatchCore → anomaly score → 임계값 비교 → OK/NG
```

---

## 2. 관련 파일 구조

```
AiServer/
├── Common/
│   ├── Config.py               # 추론 서버 설정 (StationConfig)
│   ├── ConfigLoader.py         # config.json 로더
│   ├── Inferencer.py           # Station1Inferencer (추론 로직)
│   ├── Packet.py               # TCP 패킷 빌드
│   ├── Protocol.py             # 메시지 번호 정의
│   ├── StationRunner.py        # 추론 파이프라인 (비동기 큐)
│   └── TcpClient.py            # 운용서버와 TCP 통신
├── Station1/
│   └── Station1Main.py         # 입고검사 추론 서버 진입점
├── Training/
│   ├── TrainingConfig.py       # 학습 서버 설정
│   ├── TrainingMain.py         # 학습 서버 진입점 (TCP)
│   └── TrainPatchcore.py       # PatchCore 학습 로직 (핵심)
├── tests/
│   ├── TestTraining.py         # 학습 실행 스크립트
│   ├── TestInference.py        # 단일 이미지 추론 테스트
│   ├── TestBatchInference.py   # 폴더 전체 배치 추론
│   └── DebugInference.py       # 모델 출력 디버깅
├── data/
│   └── station1/
│       ├── normal/             # 정상 이미지 (학습용)
│       ├── abnormal/           # 불량 이미지 (AUROC 검증용)
│       └── test/               # 테스트 이미지 (수동 추론용)
├── models/                     # 학습된 모델 저장 (자동 생성)
├── requirements.txt            # Python 패키지 목록
└── venv/                       # 가상환경
```

---

## 3. 학습 전 준비

### 3-1. 데이터 배치
```
data/station1/
├── normal/     — 정상 페트병 이미지 100~200장
├── abnormal/   — 불량 페트병 이미지 20장+ (AUROC 검증용)
└── test/       — 학습 후 수동 검증용 이미지 (정상+불량)
```

**권장 장수**
| 폴더 | 최소 | 권장 |
|---|---|---|
| normal | 100장 | 200~300장 |
| abnormal | 10장 | 30~50장 |
| test | 10장 | 30장+ |

### 3-2. 촬영 시나리오 (우리 프로젝트 기준)

#### 입고단계 (Station1) — 2% 빈 페트병 (캡/라벨 없음)

| 카테고리 | 세부 시나리오 | 폴더 | 실제 공장 대응 |
|---|---|---|---|
| **정상** | 깨끗한 2% 빈 페트병 | normal/ | 정상 |
| 보드마카 표식 | 겉면에 펜 자국 | abnormal/ | 잉크/기름 얼룩 |
| 이물질(흰 종이) | 하얀 종이뭉치 투입 | abnormal/ | 먼지/종이 부스러기 |
| 이물질(유색 종이) | 색깔 종이뭉치 투입 | abnormal/ | 이물질 일반 |
| 스크래치 | 커터칼로 표면 자국 | abnormal/ | 성형/운반 중 긁힘 |

#### 조립후단계 (Station2) — 2% 완성품 (캡+라벨 있음)

| 카테고리 | 세부 시나리오 | 폴더 | 실제 공장 대응 |
|---|---|---|---|
| **정상** | 2% 완성품 (캡+라벨 정상) | normal/ | 정상 |
| 라벨 기울임 | 라벨을 병에 기울여 부착 | abnormal/ | 라벨러 정렬 오차 |
| 캡 미세 열림 | 한 번 열고 다시 닫기 | abnormal/ | 캡 느슨함 (leak 위험) |
| 캡 기울어짐 | 캡을 사선으로 체결 | abnormal/ | 캡 사선 체결 |
| 캡 없음 | 캡 미부착 | abnormal/ | 라인 누락 |
| 라벨 없음 | 라벨 미부착 | abnormal/ | 라벨러 실패 |
| 라벨 위치 이동 | 라벨을 다른 위치에 부착 | abnormal/ | 라벨 부착 오프셋 |

> ⚠️ **주의**: "라벨 각도"를 촬영 각도 변경으로 시뮬레이션하면 안 된다.
> 공장에서는 카메라가 고정되어 있으므로, **병을 고정하고 라벨만 기울여서 붙이는** 방식으로 촬영해야 한다.
> 카메라 각도를 바꾸면 "촬영 각도 변동 = 정상"이라고 학습되어 오작동의 원인이 된다.

### 3-4. 가상환경 & 패키지 설치

```bash
cd /mnt/hdd/factory/code/AiServer

# 가상환경 생성 (처음 한 번만)
python3 -m venv venv

# 활성화
source venv/bin/activate

# 설치 확인
which pip
# → /mnt/hdd/factory/code/AiServer/venv/bin/pip 이어야 함

# 패키지 설치
pip install -r requirements.txt
```

### 3-5. GPU 확인
```bash
python3 -c "import torch; print('CUDA:', torch.cuda.is_available())"
```
- `CUDA: True` → GPU 사용 가능
- `CUDA: False` → CPU 학습 (느림)

---

## 4. 학습 실행

### 4-1. 기본 실행 (GPU)
```bash
python3 tests/TestTraining.py --type patchcore --station 1
```

### 4-2. 옵션 설명
| 옵션 | 설명 | 기본값 |
|---|---|---|
| `--type` | 모델 타입 (patchcore/yolo) | 필수 |
| `--station` | 스테이션 번호 (1/2) | 1 |
| `--data` | 데이터 폴더 경로 | `data/station1/normal` |
| `--device` | 연산 장치 (auto/cuda/cpu) | auto |

### 4-3. CPU 강제 실행
```bash
CUDA_VISIBLE_DEVICES="" python3 tests/TestTraining.py --type patchcore --station 1 --device cpu
```

---

## 5. 학습 과정 이해

### 5-1. 학습 단계
```
[  0%] Initializing PatchCore training...
       → Anomalib 라이브러리 초기화

[ 10%] Loading dataset...
       → data/station1/normal 폴더의 이미지 로드

[ 20%] Creating PatchCore model...
       → Wide ResNet-50 백본 모델 생성
       → ImageNet 사전학습 가중치 다운로드 (최초 1회)

[ 30%] Starting PatchCore training...
       → 특징 추출 준비

[ 40%] Training PatchCore model...
       → 정상 이미지들에서 특징 추출
       → 메모리뱅크(coreset) 구축 — PatchCore의 핵심!

[ 70%] Evaluating model...
       → abnormal/ 폴더 이미지로 AUROC 측정

[ 85%] Saving model checkpoint...
       → models/station1_patchcore_vYYYYMMDD_HHMMSS.ckpt 저장

[ 90%] Finding optimal threshold...
       → 정상/불량 점수 분포 분석
       → F1-score 최대화 임계값 자동 탐색
       → models/station1_patchcore_vYYYYMMDD_HHMMSS_threshold.json 저장

[100%] Training complete!
```

### 5-2. 핵심 개념
- **PatchCore는 1 epoch만 학습한다**
  - 일반 딥러닝처럼 가중치를 업데이트하지 않음
  - "정상 이미지의 특징을 메모리에 저장"하는 과정
- **AUROC 1.0이 나와도 과신 금지**
  - 학습-검증 데이터가 같은 분포라서 높게 나옴
  - 실제 성능은 test 폴더로 추가 검증 필요

---

## 6. 학습 결과 확인

### 6-1. 생성된 파일
```
models/
├── station1_patchcore_v20260420_161822.ckpt               # 모델 가중치
└── station1_patchcore_v20260420_161822_threshold.json     # 최적 임계값
```

### 6-2. threshold.json 내용 예시
```json
{
  "threshold": 72.85,
  "f1_score": 0.9231,
  "auto_detected": true,
  "normal_count": 120,
  "abnormal_count": 20,
  "normal_score_range": [71.15, 72.70],
  "abnormal_score_range": [72.21, 81.82]
}
```

**해석**
- `threshold: 72.85` — 이 값 초과 시 NG 판정
- `f1_score: 0.9231` — 분류 성능 (1.0에 가까울수록 좋음, 0.9+면 우수)
- `normal_score_range` — 정상 이미지 점수 범위
- `abnormal_score_range` — 불량 이미지 점수 범위

### 6-3. 기본 이름으로 복사 (추론 시 자동 로드)
```bash
cp models/station1_patchcore_v*.ckpt models/station1_patchcore.ckpt
cp models/station1_patchcore_v*_threshold.json models/station1_patchcore_threshold.json
```

---

## 7. 추론 테스트

### 7-1. 단일 이미지 추론
```bash
python3 tests/TestInference.py \
  --station 1 \
  --image data/station1/test/어느이미지.jpg
```

**결과 예시**
```
[결과]
판정:       NG
이상점수:   74.2090
결함유형:   crack
히트맵:     있음
추론시간:   120.0 ms

히트맵 저장: test_station1_heatmap.jpg
```

### 7-2. 폴더 전체 배치 추론
```bash
python3 tests/TestBatchInference.py \
  --station 1 \
  --dir data/station1/test
```

**결과 예시**
```
번호 | 파일명                    | 판정 | 이상점수 | 결함
--------------------------------------------------------------
   1 | image_001.jpg            |   OK |  33.5   | -
   2 | image_002.jpg            |   NG |  74.2   | crack
   ...

총 이미지:      50장
OK (정상):      30장 (60.0%)
NG (불량):      20장 (40.0%)
평균 추론시간:  120.0 ms/장
```

### 7-3. 임계값 수동 지정
```bash
# 더 엄격한 판정 (NG 많아짐)
python3 tests/TestBatchInference.py --station 1 --dir data/station1/test --threshold 34.0

# 더 관대한 판정 (OK 많아짐)
python3 tests/TestBatchInference.py --station 1 --dir data/station1/test --threshold 72.8
```

---

## 8. 문제 해결

### 8-1. 모든 이미지가 NG로 판정됨
- **원인**: 임계값이 너무 낮음 (0.5)
- **해결**: `threshold.json`이 제대로 로드됐는지 확인
```bash
# 파일 이름 확인
ls models/*threshold.json

# 파일이 있는데 로드 실패 → 기본 이름으로 복사
cp models/station1_patchcore_v*_threshold.json models/station1_patchcore_threshold.json
```

### 8-2. 모든 이미지가 OK로 판정됨
- **원인**: 임계값이 너무 높음
- **해결**: `--threshold` 옵션으로 낮춰서 재테스트

### 8-3. `No module named 'anomalib'`
- **원인**: venv 미활성화 또는 패키지 미설치
```bash
source venv/bin/activate
pip install -r requirements.txt
```

### 8-4. AUROC = 0.0
- **원인**: `abnormal/` 폴더에 이미지 없음
- **해결**: 불량 이미지를 `data/station1/abnormal/`에 넣고 재학습

### 8-5. 미세 결함 감지 실패
- **원인**: 종이/작은 이물질은 224x224 리사이즈 시 너무 작아짐
- **해결 방법**
  1. 더 큰/대비 강한 이물질 사용
  2. 입력 크기 키우기: `Config.py`에서 `patchcore_input_size=336`
  3. 병 영역 ROI crop 전처리 추가

---

## 9. 실전 운영 (추론 서버)

### 9-1. 추론 서버 실행
```bash
python3 -m Station1.Station1Main
```

### 9-2. 동작
- 카메라에서 이미지 수신 → 추론 → NG인 경우만 운용서버로 TCP 전송
- 운용서버는 DB에 기록 + MFC GUI로 푸시
- Arduino로 리젝터 명령 전송 (불량 배출)

### 9-3. 설정 파일
`config/config.json`:
```json
{
  "network": {
    "main_server_host": "10.10.10.130",
    "main_server_ai_port": 9000
  },
  "ai_server": {
    "station1": {
      "model_path": "./models/station1_patchcore.ckpt",
      "device": "auto",
      "anomaly_threshold": 0.5
    }
  }
}
```

---

## 10. 학습 데이터 늘리는 법

### 10-1. 자연스러운 변동 추가
- 병을 놓았다 뺐다 반복 (위치 변동)
- 각도를 미세하게 달리 (회전 변동)
- 조명을 약간 바꿔가며 촬영
- → 실제 공장 환경 시뮬레이션

### 10-2. 코드로 증강 — **PatchCore에서는 주의 필요**

> ⚠️ **경고**: PatchCore는 정상 이미지의 특징을 그대로 메모리에 저장하는 방식이므로,
> 과도한 증강(특히 회전/뒤집기)은 **불량 탐지 성능을 해친다**.
>
> 증강된 이미지가 "정상"으로 학습되면, 실전에서 비슷하게 기울어진/뒤집힌 불량도
> 정상으로 오판하게 된다.

#### PatchCore 증강 기법별 적합성

| 증강 기법 | 적합? | 이유 |
|---|---|---|
| 90/180/270° 회전 | ❌ | 회전된 불량을 정상으로 오판 |
| 수평/수직 뒤집기 | ❌ | 라벨 위치 바뀌어도 정상 판정 |
| 밝기/대비 조절 (±10%) | ⚠️ | 실제 조명 변동 범위 내에서만 |
| Gaussian 노이즈 (약) | ⚠️ | 실제 카메라 노이즈 수준만 |
| 미세 이동 (±5~10px) | ✅ | 위치 변동 시뮬레이션 |
| 미세 회전 (±3~5°) | ✅ | 공장 각도 오차 시뮬레이션 |

#### 권장: 증강 대신 실제 변동 촬영
PatchCore는 **자연스러운 변동이 포함된 실제 촬영 데이터**가 증강보다 낫다:
- 병을 놓았다 뺐다 반복하며 촬영 (위치 변동)
- 각도를 매번 조금씩 다르게 (회전 변동)
- 조명을 살짝 바꿔가며 (조명 변동)

#### 기존 증강 스크립트 (비권장)
현재 `TestTraining.py --augment` 명령은 90도 회전과 뒤집기가 포함되어 있어
**PatchCore 학습에는 추천하지 않는다**. YOLO 학습용으로는 사용 가능.

```bash
# YOLO 학습용이라면 사용 가능 (PatchCore에는 비추)
python3 tests/TestTraining.py --augment data/station2/yolo/images/train --factor 5
```

---

## 11. 전체 워크플로우 요약

```
1. 데이터 촬영 → data/station1/normal/ + abnormal/
         ↓
2. 가상환경 활성화 → source venv/bin/activate
         ↓
3. 학습 실행 → python3 tests/TestTraining.py --type patchcore --station 1
         ↓
4. 결과 확인 → models/ 폴더에 .ckpt + _threshold.json 생성
         ↓
5. 기본 이름으로 복사 → station1_patchcore.ckpt
         ↓
6. 추론 테스트 → python3 tests/TestBatchInference.py --station 1 --dir data/station1/test
         ↓
7. 성능 부족하면 → 데이터 추가 or 임계값 조정 or 입력 크기 변경
         ↓
8. 실전 배포 → python3 -m Station1.Station1Main
```
