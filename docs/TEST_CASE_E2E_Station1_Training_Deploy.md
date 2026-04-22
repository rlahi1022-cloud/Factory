# E2E 테스트: Station1 입고검사 학습 → 모델 배포 → 추론 검증

**테스트 분류**: 시스템 통합 테스트 (End-to-End)
**범위**: 학습서버 + 운용서버(MainServer) + 추론서버#1 + DB + 네트워크
**목적**: Station1 PatchCore 학습 결과물이 실제 추론서버까지 자동 배포되어 추론 가능한 상태가 되는지 검증
**작성일**: 2026-04-22

---

## 1. 시나리오 개요

```
┌────────────────┐   TRAIN_START_REQ    ┌────────────────┐   TRAIN_COMPLETE   ┌────────────────┐
│   MFC Client   │ ──────────────────>  │ Training Server │ ─────────────────> │  Main Server   │
│  (재학습 버튼) │                       │  (10.10.10.120) │    + 모델 바이너리 │  (10.10.10.130)│
└────────────────┘                      └────────────────┘                     └────────┬───────┘
                                                                                        │ MODEL_RELOAD_CMD
                                                                                        │ + 모델 바이너리
                                                                                        ▼
                                                                               ┌────────────────┐
                                                                               │ Inference #1   │
                                                                               │  (Station1)    │
                                                                               │  ← 새 모델 로드│
                                                                               └────────────────┘
```

**검증 포인트**: 새 모델로 교체된 추론서버가 실제로 **새 가중치로 추론**하는지 확인.

---

## 2. 사전 조건

### 2.1 환경

| 역할 | 호스트 | 포트 | 상태 |
|------|--------|------|------|
| Main Server (C++) | 10.10.10.130 | 9000 (AI), 9010 (GUI) | 기동 중 |
| Training Server | 10.10.10.120 | 9100 | 기동 중 |
| Inference #1 (Station1) | 로컬 PC | — | 기동 중 |
| MariaDB | 10.10.10.130 | 3306 | 접속 가능 |
| MFC Client | 임의 PC | — | 로그인 완료 |

### 2.2 데이터/파일

- [ ] `AiServer/data/station1/train/normal/` 에 학습용 정상 이미지 최소 30장 존재
- [ ] `AiServer/data/station1/test/` 에 OK/NG 샘플 이미지 존재
- [ ] `AiServer/models/` 디렉터리 생성됨 (비어 있어도 OK)
- [ ] DB `inspections` 테이블에 `heatmap_path`, `pred_mask_path` 컬럼 존재 (v0.9.0+)

### 2.3 코드/설정

- [ ] `config/config.json` 의 `network.main_server_host` 가 `10.10.10.130`
- [ ] `config/config.json` 의 `network.training_server_host` 가 `10.10.10.120`
- [ ] 각 서버가 최신 feat/dj 또는 develop 코드 기준

---

## 3. 테스트 절차

### Step 1. 각 서버 기동 & 헬스 확인

```bash
# ── Main Server (운용서버 PC) ──
cd MainServer/build
./main_server

# ── Training Server ──
cd AiServer
source venv/bin/activate       # Linux/WSL
python -m Training.TrainingMain

# ── Inference Server #1 (로컬 PC) ──
cd AiServer
.\venv\Scripts\Activate.ps1    # Windows
python -m Station1.Station1Main
```

**합격 기준**:
- [ ] Main Server 로그: `AI수신 리스너 시작 | 포트=9000`
- [ ] Main Server 로그: `HEALTH_PONG` 을 Training + Station1 양쪽에서 수신 (5초 내)
- [ ] MFC Client 상단 LED: Main / Station1 / Training **3개 모두 초록**

**실패 시 체크**:
- Python import 에러 → `pip install -r requirements.txt`
- `ConnectionRefusedError` → 방화벽/IP 화이트리스트 확인
- DB 접속 실패 → MariaDB 서비스 상태 + 계정 확인

---

### Step 2. 초기 추론 베이스라인 확보 (학습 전)

**목적**: 학습 전 모델로 테스트 이미지를 추론하여 현재 점수를 기록 → 재학습 후 비교용.

```powershell
# Station1 가상환경에서
cd AiServer
python -m tests.TestBatchInference --station 1 --dir data\station1\test --model models\station1_patchcore.ckpt > baseline_before.txt
```

**합격 기준**:
- [ ] 모든 이미지에 대해 추론 완료 (errors 0)
- [ ] `baseline_before.txt` 에 OK/NG 판정과 score 기록됨
- [ ] 3장 시각화 이미지 생성 (`test_station1_*.jpg/png`)

---

### Step 3. MFC 클라이언트에서 재학습 요청

MFC **모델 관리 페이지** → **"Station1 재학습"** 버튼 클릭.

**프로토콜 흐름**:
```
MFC        → [MainServer] RETRAIN_REQ(152)
MainServer → [Training]   TRAIN_START_REQ(1100)
Training   → [MainServer] TRAIN_START_RES(1101)
```

**합격 기준**:
- [ ] MFC 진행률 바 나타남 (0%)
- [ ] Main Server 로그: `TRAIN_START_REQ 전달 | 스테이션=1`
- [ ] Training Server 로그: `학습 시작 | 모델=PatchCore`

---

### Step 4. 학습 진행 모니터링

Training Server가 `TRAIN_PROGRESS(1102)` 를 주기적으로 발송.

**합격 기준** (5~10분 내):
- [ ] MFC 진행률 바가 0% → 100% 까지 증가 (epoch/loss 표시)
- [ ] Main Server 로그: `학습 진행률 푸시 | 스테이션=1 진행=XX%` 주기적으로 출력
- [ ] Training Server에서 각 epoch 의 loss/accuracy 로그 확인

**실패 시 체크**:
- 진행률이 멈춤 → Training Server GPU/메모리 확인
- 데이터 없음 에러 → `data/station1/train/normal/` 이미지 수량 확인 (최소 30장)

---

### Step 5. 학습 완료 → 모델 전송

학습 종료 시 `TRAIN_COMPLETE(1104)` + 모델 바이너리(.ckpt) 가 Main Server 로 전송됨.

**프로토콜 흐름**:
```
Training   → [MainServer] TRAIN_COMPLETE(1104) + 모델 바이너리 (수 MB)
MainServer                ├─ 파일 저장: storage/models/station1/v_YYYYMMDD_HHMMSS.ckpt
MainServer                ├─ DB INSERT: models 테이블
MainServer → [Training]   TRAIN_COMPLETE_ACK(1105)
MainServer → [MFC]        MODEL_DEPLOY_NOTIFY(156)  (프로토콜 154 완료 알림 포함)
MainServer → [Station1]   MODEL_RELOAD_CMD(1010) + 모델 바이너리
```

**합격 기준**:
- [ ] MFC 진행률 바 **100% + "완료"** 상태
- [ ] MFC 모델 관리 페이지에 **신규 모델 버전 추가** (ex: `v_20260422_133012`)
- [ ] Main Server 로그: `모델 파일 저장 완료 | ./storage/models/station1/...ckpt (XXXX bytes)`
- [ ] Main Server 로그: `INSERT models 성공`
- [ ] Main Server 로그: `추론서버 모델 리로드 요청 발행 | 스테이션=1`

**DB 확인**:
```sql
SELECT id, station_id, model_type, version, accuracy, deployed_at, is_active
FROM models
ORDER BY id DESC LIMIT 3;
```
- [ ] 새 행 존재 / `is_active=1` / `accuracy > 0`

**파일 확인** (운용서버 PC):
```bash
ls -la ./storage/models/station1/
```
- [ ] 새 `.ckpt` 파일 존재 / 크기 > 0

---

### Step 6. 추론서버 모델 리로드 확인

Station1 추론서버가 `MODEL_RELOAD_CMD(1010)` 를 받아 새 모델로 교체.

**합격 기준**:
- [ ] Station1 추론서버 로그: `모델 리로드 명령 수신 | 버전=v_XXX`
- [ ] Station1 추론서버 로그: `모델 파일 저장 완료 | models/station1_patchcore_v_XXX.ckpt`
- [ ] Station1 추론서버 로그: `모델 재로드 완료` 또는 `Inferencer reload success`
- [ ] Main Server 로그: `MODEL_RELOAD_RES 수신` (성공 응답)
- [ ] **추론서버 프로세스가 죽지 않음** (재시작 불필요)

**실패 시 체크**:
- 모델 파일은 받았으나 로드 실패 → 체크포인트 버전 호환성 (Anomalib 버전)
- RES 미수신 → TcpClient 양방향 통신 확인

---

### Step 7. 재학습 후 추론 비교

**목적**: 실제로 새 가중치로 추론되고 있는지 확인.

```powershell
# Station1 가상환경에서
python -m tests.TestBatchInference --station 1 --dir data\station1\test --model models\station1_patchcore.ckpt > baseline_after.txt

# 또는 MFC 에서 실제 공장 카메라 트리거
```

**합격 기준**:
- [ ] `baseline_before.txt` 와 `baseline_after.txt` 의 **score 값이 다름** (완전히 동일하면 모델이 안 바뀐 것)
- [ ] OK 이미지는 여전히 OK / NG 이미지는 여전히 NG (대부분)
- [ ] 추론 지연시간 < 500ms (정상 범위)

---

### Step 8. NG 발생 시 3장 파이프라인 확인

**목적**: 새 모델로 NG 판정된 케이스의 3장 이미지가 MFC 까지 오는지 확인.

의도적으로 NG 이미지 1장을 추론서버로 흘림 (실제 카메라 또는 `TestInference.py`).

```powershell
python -m tests.TestInference --station 1 --image data\station1\test\ng_sample.jpg --model models\station1_patchcore.ckpt
```

실운영 모드라면 StationRunner 가 자동으로 TCP 전송까지 수행.

**합격 기준 (실운영 모드)**:
- [ ] MFC Station1 페이지에 **NG 알림 팝업** + 이력 리스트 추가
- [ ] MFC 3분할 영역에 **원본 / Anomaly Map / Pred Mask** 3장 모두 표시
- [ ] Main Server 스토리지에 3개 파일 생성:
  ```
  storage/station1/20260422/ng_{ms}_original.jpg
  storage/station1/20260422/ng_{ms}_heatmap.png
  storage/station1/20260422/ng_{ms}_mask.png
  ```
- [ ] DB `inspections` 테이블에 새 행 + 세 경로 컬럼 모두 채워짐:
  ```sql
  SELECT id, station_id, result, confidence, image_path, heatmap_path, pred_mask_path
  FROM inspections ORDER BY id DESC LIMIT 1;
  ```

---

## 4. 종합 합격 기준 (한눈에)

| # | 검증 항목 | 합격 기준 | PASS/FAIL |
|---|-----------|-----------|-----------|
| 1 | 3서버 정상 기동 | MFC LED 3개 모두 초록 | |
| 2 | 학습 전 베이스라인 | baseline_before.txt 생성 | |
| 3 | 재학습 요청 | MFC 진행률 바 0% 표시 | |
| 4 | 학습 진행 | 100%까지 증가 | |
| 5 | 학습 완료 | MFC 완료 상태 + DB models INSERT | |
| 6 | 모델 파일 저장 | `./storage/models/station1/*.ckpt` 존재 | |
| 7 | 추론서버 리로드 | RELOAD_RES 성공 응답 | |
| 8 | 재추론 결과 | score 값 변경됨 | |
| 9 | NG 3장 파이프라인 | MFC 3분할 + DB 3경로 모두 기록 | |
| 10 | 프로세스 안정성 | 테스트 중 크래시 없음 | |

**최종 PASS 기준**: 10개 항목 전부 통과.

---

## 5. 실패 시 원인 분류 가이드

| 증상 | 의심 영역 | 확인 방법 |
|------|-----------|-----------|
| MFC LED 빨간색 | 헬스체크 실패 | `HEALTH_PING/PONG` 로그 확인 |
| 재학습 버튼 응답 없음 | MFC-MainServer 프로토콜 | `RETRAIN_REQ(152)` 수신 로그 |
| 학습 진행률 0% 고정 | Training Server | GPU/메모리/데이터 수량 |
| 모델 파일 전송 실패 | TCP 바이너리 | `TRAIN_COMPLETE` JSON `image_size` 필드 |
| DB INSERT 실패 | 스키마 | `heatmap_path` 컬럼 존재 여부 |
| 리로드 후 점수 동일 | 모델 실제 교체 안 됨 | Inferencer.load_model 재호출 로그 |
| 3장 중 일부만 수신 | Visualizer 오류 | AI 서버 `raw_anomaly_map/pred_mask` 반환 확인 |

---

## 6. 테스트 후 정리

```sql
-- 테스트용 모델/검사 기록 삭제 (선택)
DELETE FROM models      WHERE version LIKE 'v_test_%';
DELETE FROM inspections WHERE timestamp >= '2026-04-22';
```

```bash
# 테스트 이미지 파일 정리
rm storage/station1/20260422/ng_*
rm storage/models/station1/v_test_*.ckpt
```

---

## 7. 참고 문서

- `docs/CHANGES_v0.9.0_3image_protocol.md` — 3장 이미지 프로토콜 사양
- `DB_README.md` — 테이블 스키마
- `Protocol_README.md` — 프로토콜 번호 전체 목록
- `AiServer_README.md` — AI 서버 구성
- `Client_README.md` — MFC 클라이언트 구성

---

## 8. 수행 기록

| 실행자 | 일시 | 결과 | 특이사항 |
|--------|------|------|---------|
| | | | |
| | | | |
