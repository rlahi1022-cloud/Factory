# v0.15.0 변경 요약

v0.14.7 병합 + v0.14.9 Arduino 단순화 후, **통신 경로 정합성** 을 중심으로 발견된
P0/P1 이슈를 일괄 수정한 릴리스. 새로운 기능 추가보다는 **이전 버전에서 "연결해야 할 곳이
연결 안 된" 부분을 메우는 성격**.

**핵심 테마:**
- v0.14.9 의 PageStation1 하드코딩 제거를 서버 응답 연결로 마무리
- 학습 데이터 업로드 파이프라인의 ACK 재전송 경로 정상화
- 헬스체크 응답 형식을 문서 스펙과 일치
- JSON 파싱 edge case, 재학습 UX, 모델 ID 추적 개선

---

## 1. 클라이언트 (MFC, Factory_UI_CL)

### 1.1 MODEL_LIST_RES → PageStation1 연결 ⭐ 핵심
| 파일 | 변경 |
|---|---|
| `PageStation1.h` | `UpdateModelInfo(const std::string& json)` 메서드 선언 추가 |
| `PageStation1.cpp` | 동일 메서드 구현 — `items[]` 에서 `station_id==1 && is_active==1` 인 모델 골라 상단 "검사 설정" 라벨(모델명/버전/입력크기/임계값/백본) 을 서버값으로 주입 |
| `MainTabDlg.cpp:942` | `MODEL_LIST_RES` case 분기에 `m_st1->UpdateModelInfo(*pJson);` 추가 |

**배경**: v0.14.9 에서 PageStation1.cpp 에 하드코딩돼 있던 `"PatchCore v1.2.0"` 등 정적 문구를
제거하고 `"로딩 중..."`/`"-"` 로 교체했지만, **이를 서버 응답과 연결하는 코드가 MainTabDlg 에
들어있지 않아** 라벨이 영구적으로 초기 텍스트에 고정되는 상태였음. v0.15.0 에서 배선 완료.

### 1.2 재학습 중복 클릭 가드
| 파일 | 변경 |
|---|---|
| `PageModel.cpp::OnBtnRetrain` | 기존 `if (m_training) return;` (조용한 무시) → MessageBox 로 "이미 진행 중" 명시 피드백. 빈 파일 리스트 케이스도 별도 메시지로 분기. |

이전엔 사용자가 재클릭하면 반응 없이 그대로 — "왜 안 되지?" 하는 혼선 발생. 이제 명시적 팝업.

### 1.3 `CPacketBuilder::ExtractString` 백슬래시 카운팅
| 파일 | 변경 |
|---|---|
| `PacketBuilder.cpp:155-168` | `json[i-1]=='\\'` 한 글자만 보던 로직 → **직전 백슬래시 개수를 세서 짝수이면 종료 따옴표로 인정**. `\\"` 같이 `\\` + `"` 가 연속될 때 기존 로직이 `\\"` 전체를 escape 로 오판해 조기 종료하던 경계 버그 차단. |

```cpp
// 이전
if (json[i] == '"' && (i == 0 || json[i - 1] != '\\')) { lastQuote = i; break; }

// v0.15.0
if (json[i] == '"') {
    int bsCount = 0;
    for (int j = i-1; j >= firstQuote+1 && json[j]=='\\'; --j) bsCount++;
    if ((bsCount % 2) == 0) { lastQuote = i; break; }
}
```

---

## 2. 메인서버 (C++)

### 2.1 `requires_ack()` 에 업로드 계열 추가
| 파일 | 변경 |
|---|---|
| `common/Protocol.h:121` | `RETRAIN_UPLOAD(158)` + `TRAIN_DATA_UPLOAD(1108)` 을 ACK 필수로 분류 |

v0.13.0 도입 당시 ACK 정책에 누락되어, 클라→메인 학습 이미지 업로드와 메인→학습 중계 경로에
재전송 정책이 적용되지 않고 있었음. 네트워크 순단 시 **학습셋 누락으로 학습이 부분 데이터로
실행될 수 있는 잠재 위험**을 수정.

### 2.2 ConnectionPool 주석 보강
| 파일 | 변경 |
|---|---|
| `src/storage/connection_pool.cpp:124-131` | nullptr 을 `all_conns_` 에 저장하는 의도(shutdown 의 이미 해제된 old 를 다시 close 하지 않기 위함)를 주석으로 명확화. shutdown 쪽에서 `if (conn) mysql_close(conn)` 로 nullptr 방어가 이미 되어있음을 확인. |

v0.15.0 리뷰에서 "nullptr 저장은 double-close 위험" 지적이 있었으나, 실제 shutdown 코드를
교차 확인한 결과 **이미 안전한 구현**이었음. 주석에 의도를 기록해 재오인 방지.

---

## 3. AI 서버 (Python)

### 3.1 `HEALTH_PONG(1201)` 에 `server_type` 필드 명시
| 파일 | 변경 |
|---|---|
| `Common/TcpClient.py:391` | `_handle_health_ping` 의 `pong_body` 에 `"server_type": "station1"|"station2"` 추가 |

기존엔 `station_id` 필드만 보내 Router 가 유추하는 방식이었는데, `Protocol_README.md` 및
`README.md(v0.11.0)` 가 "server_type 포함" 으로 문서화하고 있어 **문서-코드 불일치**였음.
학습서버는 이미 v0.14.7 에서 `_notify_recv_loop` 가 `"training"` 으로 응답 중이었음. 통일 완료.

### 3.2 `ACK_REQUIRED_NOS` 와 C++ `requires_ack()` 정합성
| 파일 | 변경 |
|---|---|
| `Common/Protocol.py:126` | `RETRAIN_UPLOAD(158)`, `TRAIN_DATA_UPLOAD(1108)` 추가. AiServer 가 이 프로토콜 송신자는 아니지만 공통 정의로 유지. |

### 3.3 Inferencer `active_model_id` + INSPECT_META 연결
| 파일 | 변경 |
|---|---|
| `Common/Inferencer.py:144` | `BaseInferencer.active_model_id: int = 0` 프로퍼티 신설 |
| `Common/StationRunner.py:_handle_model_reload` | `MODEL_RELOAD_CMD` 의 `model_db_id` 필드(선택)를 읽어 `self._inferencer.active_model_id` 에 저장 |
| `Common/StationRunner.py:_send_inspect_meta` | `"model_id": 0` 하드코딩 → `int(getattr(self._inferencer, "active_model_id", 0))` 로 동적 참조 |

**효과**: MainServer 가 `MODEL_RELOAD_CMD` 송신 시 `model_db_id` 필드를 동봉하기 시작하면
**별도 수정 없이 INSPECT_META 에 실제 DB 모델 id 가 기록됨**. 동봉 전까지는 0 유지 (이전 호환).

### 3.4 `TcpClient.py` 임시파일 정리 예외 처리
| 파일 | 변경 |
|---|---|
| `Common/TcpClient.py:449` | `except: pass` → `except OSError as rm_exc: logger.warning(...)`. 수백 MB 모델 임시본이 조용히 쌓이던 문제 추적 가능화. |

---

## 4. 프로토콜/이벤트 변경 요약

| Protocol | 방향 | 신규/변경 필드 |
|---|---|---|
| `HEALTH_PONG(1201)` | AI/학습 → 메인 | `"server_type"` 필드 명시 추가 |
| `MODEL_RELOAD_CMD(1010)` | 메인 → 추론 | (향후) `"model_db_id"` 선택 필드 지원 수신측 준비 완료 |
| `INSPECT_META(1006)` | 추론 → 메인 | `"model_id"` 를 Inferencer 의 active_model_id 로 동적 치환 |
| `RETRAIN_UPLOAD(158)` | MFC → 메인 | ACK 필수로 분류 변경 |
| `TRAIN_DATA_UPLOAD(1108)` | 메인 → 학습 | ACK 필수로 분류 변경 |

---

## 5. 문서 업데이트

| 파일 | 섹션 |
|---|---|
| `README.md` | v0.14.5~v0.14.7 / v0.14.9 / v0.15.0 섹션 신설 |
| `Protocol_README.md` | INSPECT_NG_PUSH(110) / MODEL_LIST_RES(151) / INSPECT_CONTROL(160/161) / HEALTH_PONG(1201) 본문 JSON 예시 추가, ACK 필수 메시지 목록 업데이트 |
| `Client_README.md` | NG_PUSH 필드 `id`, `heatmap_size`, `pred_mask_size` 명시. 재학습 중복 차단 추가 |
| `AiServer_README.md` | pause/resume 최종판 섹션, HEALTH_PONG 양방향, YOLO 레이아웃 자동 감지, INSPECT_META model_id 연결 섹션 신설 |
| `Directory_README.md` | `res/CameraView.cpp`, `res/PageStation1.cpp` 항목(빌드 제외 보관본) 기재 |
| `DB_README.md` | `models.trained_by FK` 누락, `bottles` 컬럼 구성 차이(기획서 v0.12 vs 실제 schema.sql) 주의사항 추가 |
| `docs/CHANGES_v0.15.0.md` | (이 파일) 신규 작성 |

---

## 6. 적용 절차

### 6.1 메인서버 (리빌드)
```bash
cd /home/lms/Desktop/Factory/MainServer/build
cmake --build . -- -j$(nproc)
./factory_main_server
```
`requires_ack()` 가 헤더이므로 해당 헤더 include 하는 모든 .cpp 재컴파일 필요.

### 6.2 AI 서버 (재시작)
```bash
pkill -9 -f "Station1.Station1Main"
pkill -9 -f "Station2.Station2Main"
pkill -9 -f "Training.TrainingMain"

python -m Station1.Station1Main
python -m Station2.Station2Main
python -m Training.TrainingMain
```

### 6.3 클라이언트 (MFC 리빌드)
Visual Studio → Clean → Rebuild. 기존 `Factory_UI_CL.exe` 완전 종료 후 새 빌드 실행.

---

## 7. Known Issues / Followups

- **MainServer 가 `MODEL_RELOAD_CMD` 송신 시 `model_db_id` 동봉은 미구현**. v0.15.0 수신측은
  준비 완료이므로, 서버 `gui_notifier.cpp` 또는 `train_handler.cpp` 에서 TRAIN_COMPLETE 로
  INSERT 된 모델의 AUTO_INCREMENT id 를 추적해 RELOAD 시 포함하는 작업이 다음 버전 대상.
- **models.trained_by FK** 와 **bottles 테이블 컬럼 구성**은 기획서 v0.12 와 실제 스키마가
  어긋나 있음. 운영에 영향 없지만 차기 버전에서 결정 필요.
- **CameraView `res/` 보관본**은 vcxproj 에 포함되지 않은 상태. 활성화/삭제/유지 중 선택이
  필요하며 v0.15.0 에서는 현상 유지 + 문서화만 함.
- **Arduino**: v0.14.9 통합본(`arduino_led_control.ino`) 은 기획서의 `Station1Rejecter.ino`
  / `Station2Alerter.ino` 분리 설계와 다른 단일 파일 구조. 구조 차이 설명이 필요하면 별도
  `Arduino_README.md` 신설 권장.
