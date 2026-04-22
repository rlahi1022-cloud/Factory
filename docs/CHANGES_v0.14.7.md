# v0.14.7 변경 요약

세션 기간 동안 클라이언트/메인서버/AI서버에 적용된 수정 전체 목록. 핵심 테마는:
- **실시간 연결 안정성** (끊김/재접속 UX 개선)
- **더미 데이터 제거** (실서버 데이터만 UI 에 반영)
- **학습/추론 서버 LED 정상 동기화**
- **Start/Stop pause/resume 로직 단순화**

---

## 1. 클라이언트 (MFC, Factory_UI_CL)

### 1.1 연결 안정성
| 파일 | 변경 |
|---|---|
| `PacketBuilder.cpp` | `ParseHeader` JSON 상한 64KB → **1MB** (200건 이력 응답 ~88KB 가 64KB 를 초과해 끊기던 문제 해결) |
| `NetworkClient.cpp` | `RecvN` 에서 `WSAETIMEDOUT` 을 치명적 에러로 취급하던 로직 수정 → 재시도 (큰 NG_PUSH 중간 스톨에도 연결 유지). 2분 하드리밋으로 무한 대기 방어 |
| `MainTabDlg.cpp` | `IDT_RECONNECT` 10초 → **2초** 로 단축 + 성공할 때까지 끈질기게 재시도. 실패 팝업 없음 (조용히 백그라운드 재접속) |
| `MainTabDlg.cpp` | `m_userDisconnected` 플래그 도입 — 사용자가 명시적으로 로그아웃/종료하지 않는 이상 상태바에 항상 "연결됨" 표시. micro-disconnect 를 UI 가 노출 안 함 |

### 1.2 더미 데이터 제거
| 변경 |
|---|
| `IDT_LIVE_UPDATE` 타이머의 `GenRecord()` 랜덤 레코드 생성 삭제 (OK/NG 깜빡임 주범) |
| `GenInitialHistory()` (시작 시 20건 주입) 삭제 — 실서버 DB 이력만 사용 |
| `InspectionData.cpp/h` 에서 `GenRecord` / `GenInitialHistory` 선언·구현 완전 제거 |
| PageStation1/2 생성자: 초기 `score=0.12` → **0.0**, `id=10000` → 0 |
| PageStation1 Manual OK/NG/Arduino 버튼 제거 (RC + 메시지맵 + 핸들러 + 선언 모두) |
| PageHome Uptime `98.7%` 하드코딩 제거 → Uptime 영역 자체 삭제 |
| 모델정보 표시는 남김 (정적 라벨) |

### 1.3 Summary / NG 리스트
| 변경 |
|---|
| PageHome Summary 컬럼 재정렬: Total/OK/NG/DefectRate 4개 컬럼 100px 간격, 라벨·값 모두 `SS_CENTER` |
| PageHome `m_cumOk[3]`, `m_cumNg[3]` **누적 카운터** 도입. `Update(recs)` 의 50건 cap 의존성 제거 |
| `ApplyStatsRes(json)` 신규 — 로그인 직후 `STATS_RES(131)` 로 DB 절대값 수신 → Summary 초기화 |
| `UpdateStationCount` 는 `OK_COUNT_PUSH(112)` 서버 절대값으로 덮어쓰기 (station 별) |
| `AddNgRow` 는 NG push 때마다 station 별 `m_cumNg` +1 (다음 `OK_COUNT_PUSH` 에서 자동 수렴) |
| NG 실시간 push 수신 시 `rec.id = m_nextId++` (로컬 카운터) → **서버가 보낸 `"id"` (DB AUTO_INCREMENT) 사용** |
| PageHome NG 리스트 **더블클릭** → `INSPECT_IMAGE_REQ(116)` 전송 + 해당 Station 탭으로 자동 전환 |
| PageStation1 NG 리스트 `PopulateNgHistoryFromJson(json)` 추가 — 로그인 직후 DB 이력으로 하단 10건 초기 채움 (텍스트 전용) |

### 1.4 Station 페이지 실시간 결과 표시
| 변경 |
|---|
| Station1/2 `Update()` 가 **NG 레코드만** 반영 (OK 깜빡임 차단) |
| Result 라벨: NG 면 "NG" + 점수 / OK·대기 면 "--" + "이상 점수: 0.00" (임계값 표기 제거) |
| CameraView 하단 빨강 NG 배지 제거 (`DrawBadge` 호출 주석 처리) |
| CameraView 의 `DrawYolo` / `DrawNgBox` / `DrawScoreBar` / `draw_mask_circles` 등 **미사용 수동 드로잉 함수 전량 삭제** |

### 1.5 서버 상태 LED (Tri-state)
| 변경 |
|---|
| `bool m_sv0/1/2` → `enum class ServerState { Unknown, Up, Down }` |
| 초기값 `true` (무조건 초록) → `Unknown` (회색) — 서버 HEALTH_PUSH 오기 전까지 "상태 모름" |
| `DrawLed(bool)` → `DrawLed(ServerState)` 로 시그니처 변경. 회색(RGB 160,160,160) 추가 |

### 1.6 깜빡임 제거
| 변경 |
|---|
| `MainTabDlg::OnEraseBkgnd` — 전체 클라이언트 영역을 채우던 코드 제거. `return TRUE` 만 |
| `MainTabDlg::OnPaint` — 메모리 DC 더블버퍼링 (배경 + 타이틀 + 툴바 + 상태바를 backbuffer 에 그린 뒤 `BitBlt`) |
| 모든 `InvalidateRect(..., ...)` 호출에 `FALSE` (배경 지움 생략) 지정 |
| PageStats 차트 OnPaint 도 더블버퍼링 적용 + `OnEraseBkgnd` 추가 |

### 1.7 업로드
| 변경 |
|---|
| PageModel 폴더 스캔 확장자: `*.jpg`, `*.png` → `*.jpg`, `*.jpeg`, `*.png`, `*.bmp` (Basler Pylon 산업카메라 BMP 저장본 수용) |

---

## 2. 메인서버 (C++)

### 2.1 접속/해제 로그 debounce
| 파일 | 변경 |
|---|---|
| `session_manager.cpp/h` | `flush_expired_disconnects()` 신규. "pending 해제" 맵(`g_recent_dc`) 으로 로그 지연 발행 |
| `session_manager.cpp` | `register_session`: 같은 IP 가 `RECONNECT_WINDOW=3초` 안에 재접속하면 접속/해제 로그 **둘 다 생략** |
| `session_manager.cpp` | `unregister_session`: 로그를 즉시 찍지 않고 pending 맵에 등록만 |
| `gui_tcp_listener.cpp` | `run_accept_loop` 가 매 1초(accept 타임아웃 주기) `flush_expired_disconnects()` 호출 → 3초 넘게 재접속 없으면 **"클라이언트 해제 | ... (재접속 없음 — 진짜 해제)"** 로그 발행 |

### 2.2 서버 상태 LED 동기화
| 파일 | 변경 |
|---|---|
| `health_checker.cpp` | 매 tick 마다 `SERVER_DOWN` 또는 `SERVER_RECOVERED` 이벤트 **무조건 publish** (과거엔 상태 전환 시에만) → GUI 클라가 뒤늦게 접속해도 다음 tick 에 HEALTH_PUSH 수신 |
| `gui_router.cpp` | 로그인 직후 초기 sync 매칭: `target.ip` prefix → `ConnectionRegistry::server_type` 으로 전환. config 의 `ip=""` 동적 설정에서도 제대로 매칭 |
| `gui_router.cpp` | 초기 sync 진단 로그 추가 (`초기 HEALTH_PUSH 송신 | target=... status=...`) |
| `gui_notifier.cpp` | 매 tick 반복되는 `log_push("서버 장애 감지/복구 감지")` 노이즈 억제 — 상태 전환은 HealthChecker 측 `log_main` 에서 이미 기록 |

### 2.3 JSON 프로토콜
| 파일 | 변경 |
|---|---|
| `gui_notifier.cpp` | NG_PUSH(110) JSON 에 `"id": <DB AUTO_INCREMENT>` 필드 추가 (v0.14.7 기본 커밋) |
| `inspection_service.cpp` | `InspectionEvent.db_id` 채워서 publish — GUI 측 리스트 중복방지 키 |

### 2.4 검사 제어
| 파일 | 변경 |
|---|---|
| `gui_router.cpp` | `INSPECT_NG_ACK_EXT(111)` 수신 시 "NG ACK 수신" 로그 추가 — 클라 해제 직전에 찍히면 정상 ACK 후 끊김, 안 찍히면 크래시 의심 (진단용) |

---

## 3. AI 서버 (Python)

### 3.1 학습서버 (TrainingMain.py)
| 변경 |
|---|
| `_notify_recv_loop()` 추가 — main 서버의 notify 채널이 send-only 였던 것을 bidirectional 로 전환 |
| 수신 루프가 `HEALTH_PING(1200)` 받으면 `HEALTH_PONG(1201)` + `server_type="training"` 회신 → main 의 `ConnectionRegistry` 가 `ai_training` 으로 태깅 → LED 초록 전환 |
| EOF / 예외 시 루프 탈출 (다음 `_send_to_main` 호출 때 자동 재접속) |

### 3.2 YOLO 학습 데이터 레이아웃 자동 감지
| 파일 | 변경 |
|---|---|
| `TrainYolo.py` | `_detect_yolo_layout(data_dir)` 신규. 5가지 레이아웃 자동 감지: 표준(`images/train`, `images/val`) / Roboflow(`train/images`, `valid/images`, `test/images`) / Roboflow-val / Flat / Flat-valid |
| `TrainYolo.py` | `create_data_yaml` 이 항상 실제 폴더 구조에 맞게 `data.yaml` 을 **덮어쓰기** |
| `TrainingMain.py` | `_train_yolo` 에서 기존 `data.yaml` 있어도 무조건 재생성 (Roboflow 구조에서도 그대로 학습 가능) |

### 3.3 검사 pause/resume 단순화
| 파일 | 변경 |
|---|---|
| `StationRunner.py` | 이전: pause 시 `camera.stop_grabbing()`, resume 시 `camera.start_grabbing()` — Pylon Stop/Start 사이클이 짧게 반복되면 두 번째 이후 Start 에서 프레임 안 나오던 문제 |
| `StationRunner.py` | 이후: **카메라 HW 는 항상 grab 상태 유지**, `_pause_event` 토글로만 grab_producer 루프 제어. `GrabStrategy_LatestImageOnly` 가 버퍼에 최신 1프레임만 남기므로 stale 누적 없음 |
| `PylonCamera.py` | `start_grabbing()` / `stop_grabbing()` 메서드는 유지 (shutdown 경로에서 사용) |

### 3.4 AI 서버 공통 (Common/)
| 파일 | 변경 |
|---|---|
| `TcpClient.py` | `INFERENCE_CONTROL_CMD(1020)` 수신 처리 + `_on_inference_control` 콜백 |
| `PylonCamera.py` | `_detect_yolo_layout` 지원은 학습서버 전용이라 해당 사항 없음 |

---

## 4. 프로토콜/이벤트 변경 요약

| Protocol | 방향 | 추가 필드 |
|---|---|---|
| `NG_PUSH(110)` | 서버→클라 | `"id": <DB id>`, `"timestamp"`, `"score"`, `"latency_ms"` |
| `HEALTH_PUSH(170)` | 서버→클라 | (기존과 동일) — 이제 매 tick 브로드캐스트 |
| `INFERENCE_CONTROL_CMD(1020)` | 서버→추론 | action: pause/resume |
| `INFERENCE_CONTROL_RES(1021)` | 추론→서버 | server_type, paused, success, message |
| `HEALTH_PING(1200)` | 서버→AI | (기존) — 이제 학습서버도 notify 채널로 받아 응답 |
| `HEALTH_PONG(1201)` | AI→서버 | `server_type: "training"/"station1"/"station2"` |

---

## 5. Known Issues / Followups

- 로그인 → history(200건) 응답 → NG 리스트/Summary 일시적으로 0 이었다가 STATS_RES(131) 도착 시 누적값으로 점프. **의도된 동작** (초기 DB 스냅샷) — 사용자 혼동 줄이려면 "로딩 중..." 표시 추가 필요.
- HealthChecker 매 tick broadcast 로 NG_PUSH 와 동시에 HEALTH_PUSH 가 같이 쏟아질 때, 송신 큐 포화 가능성. 현재는 drop-oldest 로 방어.
- Manual 테스트 버튼 제거로 RC 리소스 ID 는 남아있음 (`IDC_BTN_S1_OK` 등). 다음 RC 청소 때 삭제 권장.
