# v0.14.7 변경 요약

세션 기간 동안 클라이언트/메인서버/AI서버에 적용된 수정 전체 목록.

**핵심 테마:**
- 실시간 연결 안정성 (끊김 / 재접속 UX)
- 더미 데이터 제거 (실서버 데이터만 UI 에 반영)
- 학습/추론 서버 LED 정상 동기화
- 검사 Start/Stop pause/resume 완전 안정화

---

## 1. 클라이언트 (MFC, Factory_UI_CL)

### 1.1 연결 안정성
| 파일 | 변경 |
|---|---|
| `PacketBuilder.cpp` | `ParseHeader` JSON 상한 64KB → **1MB**. 200건 이력 응답(~88KB)이 64KB 초과로 끊기던 문제 해결 |
| `NetworkClient.cpp` | `RecvN` 에서 `WSAETIMEDOUT` 을 치명적 에러로 취급하던 로직 수정 → 재시도. 2분 하드리밋으로 무한 대기 방지 |
| `MainTabDlg.cpp` | `IDT_RECONNECT` 10초 → **2초** 로 단축 + 성공할 때까지 반복. 실패 팝업 없음 (조용히 백그라운드 재접속) |
| `MainTabDlg.cpp/h` | `m_userDisconnected` 플래그 — 사용자가 명시적으로 로그아웃/종료하지 않으면 상태바에 항상 "연결됨". micro-disconnect 를 UI 가 노출 안 함 |

### 1.2 더미 데이터 제거
| 변경 |
|---|
| `IDT_LIVE_UPDATE` 의 `QCUtil::GenRecord()` 랜덤 레코드 생성 삭제 — OK/NG 깜빡임 주범 |
| `GenInitialHistory()` (시작 시 20건 주입) 삭제 |
| `InspectionData.cpp/h` 에서 `GenRecord` / `GenInitialHistory` 완전 제거 |
| PageStation1/2 생성자: 초기 `score=0.12/0.15` → **0.0**, `id=10000` → 0, `latencyMs` → 0 |
| PageStation1 Manual OK/NG/Arduino 버튼 제거 (RC + 메시지맵 + 핸들러 + 선언) |
| PageHome Uptime 98.7% 하드코딩 및 영역 자체 삭제 |

### 1.3 Summary / NG 리스트
| 변경 |
|---|
| PageHome Summary 컬럼 재정렬: Total/OK/NG/DefectRate 4개, 100px 간격, 라벨·값 모두 `SS_CENTER` |
| `m_cumOk[3]`, `m_cumNg[3]` **누적 카운터** 도입. `Update(recs)` 의 50건 cap 의존성 제거 |
| `ApplyStatsRes(json)` 신규 — 로그인 직후 `STATS_RES(131)` 로 DB 절대값 수신 → Summary 초기화 |
| `UpdateStationCount` 는 `OK_COUNT_PUSH(112)` 서버 절대값으로 덮어쓰기 |
| `AddNgRow` 는 NG push 때마다 station 별 `m_cumNg` +1 (다음 OK_COUNT_PUSH 에서 자동 수렴) |
| NG 실시간 push 수신 시 `rec.id`를 **서버가 보낸 `"id"` (DB AUTO_INCREMENT) 사용** (이전 `m_nextId++` 로컬 카운터 폐기) |
| PageHome NG 리스트 **더블클릭** → `INSPECT_IMAGE_REQ(116)` 전송 + 해당 Station 탭으로 자동 전환 |
| PageStation1 `PopulateNgHistoryFromJson(json)` 추가 — 로그인 직후 DB 이력으로 하단 10건 초기 채움 (텍스트 전용) |

### 1.4 Station 페이지 실시간 결과
| 변경 |
|---|
| Station1/2 `Update()` 가 **NG 레코드만** 반영 (OK 깜빡임 차단) |
| Result 라벨: NG 면 "NG" + 점수 / OK·대기 면 "--" + "이상 점수: 0.00" (임계값 표기 제거) |
| CameraView 하단 빨강 NG 배지 제거 (`DrawBadge` 호출 주석) |
| CameraView `DrawYolo` / `DrawNgBox` / `DrawScoreBar` / `draw_mask_circles` 미사용 함수 전량 삭제 |

### 1.5 서버 상태 LED (Tri-state)
| 변경 |
|---|
| `bool m_sv0/1/2` → `enum class ServerState { Unknown, Up, Down }` |
| 초기값 `true` → `Unknown` (회색). HEALTH_PUSH 수신 전까지 "상태 모름" 표시 |
| `DrawLed(bool)` → `DrawLed(ServerState)`. 회색(160,160,160) 추가 |

### 1.6 깜빡임 제거
| 변경 |
|---|
| `MainTabDlg::OnEraseBkgnd` — 전체 영역 채우던 코드 제거, `return TRUE` 만 |
| `MainTabDlg::OnPaint` — 메모리 DC 더블버퍼링 + `BitBlt` |
| 모든 `InvalidateRect(...)` 호출에 `FALSE` (배경 지움 생략) |
| PageStats `OnPaint` 도 더블버퍼링 + `OnEraseBkgnd` 추가 |

### 1.7 업로드
| 변경 |
|---|
| PageModel 폴더 스캔 확장자: `.jpg`, `.png` → `.jpg`, `.jpeg`, `.png`, `.bmp` (Basler Pylon BMP 저장본 수용) |

---

## 2. 메인서버 (C++)

### 2.1 접속/해제 로그 debounce
| 파일 | 변경 |
|---|---|
| `session_manager.cpp/h` | `flush_expired_disconnects()` 신규. IP 별 "pending 해제" 맵(`g_recent_dc`) 운영 |
| `session_manager.cpp` | `register_session`: 같은 IP 가 `RECONNECT_WINDOW=3초` 안에 재접속하면 접속/해제 로그 **둘 다 생략** |
| `session_manager.cpp` | `unregister_session`: 로그 즉시 출력 안 하고 pending 에 등록만 |
| `gui_tcp_listener.cpp` | `run_accept_loop` 가 1초마다 `flush_expired_disconnects()` 호출 → 3초 넘게 재접속 없을 때만 **"클라이언트 해제 | ... (재접속 없음 — 진짜 해제)"** 로그 |

### 2.2 서버 상태 LED 동기화
| 파일 | 변경 |
|---|---|
| `health_checker.cpp` | 매 tick **무조건** SERVER_DOWN / SERVER_RECOVERED 이벤트 publish (과거엔 상태 전환 시에만) → GUI 클라가 뒤늦게 접속해도 다음 tick 에 HEALTH_PUSH 수신 |
| `gui_router.cpp` | 초기 sync 매칭: `target.ip` prefix → `ConnectionRegistry::server_type`. config 의 `ip=""` 동적 설정에서도 매칭 성공 |
| `gui_router.cpp` | 초기 sync 진단 로그 `초기 HEALTH_PUSH 송신 | target=... status=...` 추가 |
| `gui_notifier.cpp` | 매 tick 반복 로그 노이즈 억제 — 상태 전환은 HealthChecker 의 `log_main` 에만 기록 |

### 2.3 프로토콜
| 파일 | 변경 |
|---|---|
| `gui_notifier.cpp` | `NG_PUSH(110)` JSON 에 `"id": <DB AUTO_INCREMENT>` 필드 추가 |
| `inspection_service.cpp` | `InspectionEvent.db_id` 채워서 publish — GUI 리스트 중복방지 키 |

### 2.4 검사 제어 진단
| 파일 | 변경 |
|---|---|
| `gui_router.cpp` | `INSPECT_NG_ACK_EXT(111)` 수신 시 `NG ACK 수신` 로그 — 해제 직전에 찍히면 정상, 안 찍히면 크래시 의심 (진단용) |

---

## 3. AI 서버 (Python)

### 3.1 학습서버 (TrainingMain.py)
| 변경 |
|---|
| `_notify_recv_loop()` 추가 — main 서버 notify 채널이 send-only 였던 것을 bidirectional 로 |
| 수신 루프가 `HEALTH_PING(1200)` 받으면 `HEALTH_PONG(1201)` + `server_type="training"` 회신 → main 의 `ConnectionRegistry` 가 `ai_training` 으로 태깅 → LED 초록 전환 가능 |
| EOF / 예외 시 루프 탈출 (다음 `_send_to_main` 호출 때 자동 재접속) |

### 3.2 YOLO 학습 데이터 레이아웃 자동 감지
| 파일 | 변경 |
|---|---|
| `TrainYolo.py` | `_detect_yolo_layout(data_dir)` 신규. 5가지 레이아웃 자동 감지: 표준 / Roboflow(valid) / Roboflow-val / Flat / Flat-valid |
| `TrainYolo.py` | `create_data_yaml` 이 항상 실제 폴더 구조에 맞게 `data.yaml` **덮어쓰기** |
| `TrainingMain.py` | `_train_yolo` 에서 기존 `data.yaml` 있어도 무조건 재생성 → Roboflow 구조 그대로 학습 가능 |

### 3.3 검사 pause/resume 완전 단순화 🟢 최종판
| 파일 | 변경 |
|---|---|
| `StationRunner._handle_inference_control` | **이벤트 토글만** 수행. `camera.stop_grabbing()` / `camera.start_grabbing()` 호출 **완전 제거** |
| `pause`: `self._pause_event.clear()` 한 줄 |
| `resume`: `self._pause_event.set()` 한 줄 |

**왜 이 변경이 필수였나 (증상/원인/해결 맥락):**

| 버전 | 동작 | 증상 |
|---|---|---|
| v0.14.0 | 이벤트 토글만 | 카메라 HW 는 계속 grab — OK |
| v0.14.5 | 이벤트 + `stop_grabbing/start_grabbing` | "카메라도 멈춰야 한다" 요구로 HW stop/start 추가. **두 번째 이후 Start 에서 프레임 안 나오는 간헐적 실패** (Pylon 드라이버 stop/start 사이클 불안정) |
| v0.14.7 **최종** | 이벤트 토글만 (v0.14.0 회귀) | Pylon `GrabStrategy_LatestImageOnly` 가 pause 동안 최신 1프레임만 유지하므로 stale 누적 없음. 1번 클릭 = 100% 즉시 반응 |

**Tradeoff:** 카메라 HW 전력은 항상 on (미세 차이). 안정성이 훨씬 중요하므로 채택.

---

## 4. 프로토콜/이벤트 변경 요약

| Protocol | 방향 | 신규/변경 필드 |
|---|---|---|
| `NG_PUSH(110)` | 서버→클라 | `"id": <DB id>`, `"timestamp"`, `"score"`, `"latency_ms"` |
| `HEALTH_PUSH(170)` | 서버→클라 | (기존) — 이제 매 tick 브로드캐스트 |
| `INFERENCE_CONTROL_CMD(1020)` | 서버→추론 | `action`: pause / resume |
| `INFERENCE_CONTROL_RES(1021)` | 추론→서버 | `server_type`, `paused`, `success`, `message` |
| `HEALTH_PING(1200)` | 서버→AI | (기존) — 학습서버도 notify 채널로 받아 응답 |
| `HEALTH_PONG(1201)` | AI→서버 | `server_type: "training" / "station1" / "station2"` |

---

## 5. 적용 절차

### 5.1 메인서버
```bash
cd /home/lms/Desktop/Factory/MainServer/build
make -j$(nproc)
./factory_main_server
```

### 5.2 AI 서버 (Python)
```bash
# 기존 프로세스 강제 종료
pkill -9 -f "Station1.Station1Main"
pkill -9 -f "Station2.Station2Main"
pkill -9 -f "Training.TrainingMain"

# 재실행
python -m Station1.Station1Main          # 추론 PC1
python -m Station2.Station2Main          # 추론 PC2
python -m Training.TrainingMain          # 학습 PC
```
Python 은 "파일 저장 → 프로세스 재시작" 만으로 반영. 기존 실행 중인 인터프리터는 옛 코드를 계속 쓰므로 재시작이 필수.

### 5.3 클라이언트 (MFC)
Visual Studio → 솔루션 정리(Clean) → 리빌드. Windows PC 에서 이전 `Factory_UI_CL.exe` 프로세스 완전 종료 후 새 빌드 실행.

---

## 6. Known Issues / Followups

- 로그인 → history(200건) → NG 리스트/Summary 일시적 0 → STATS_RES(131) 도착 시 누적값으로 점프. **의도된 동작** (DB 스냅샷 초기화). "로딩 중..." 표시 추가하면 UX 개선 가능.
- HealthChecker 매 tick broadcast 로 NG_PUSH 와 HEALTH_PUSH 가 겹치면 session 송신 큐 포화 가능. 현재는 drop-oldest 로 방어.
- Manual 테스트 버튼 제거 후에도 RC 리소스 ID(`IDC_BTN_S1_OK` 등) 는 `Resource.h` 에 남아있음. 다음 RC 청소 때 삭제 권장.
- ~~README / Protocol_README / AiServer_README 등 MD 문서는 v0.14.3~0.14.5 수준에 머물러 있어 별도 업데이트 필요~~ → **v0.15.0 에서 일괄 반영** (`docs/CHANGES_v0.15.0.md` 참조):
  - ✅ `NG_PUSH(110)` JSON 예시에 `"id"` 필드 추가 (Protocol_README, Client_README)
  - ✅ 학습서버 notify 양방향 통신 문단 추가 (AiServer_README)
  - ✅ 세션 해제 debounce 규칙은 README 의 v0.14.7 섹션 참고
