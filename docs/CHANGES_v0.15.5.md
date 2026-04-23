# v0.15.5 — Hotfix: Config 객체 배열 파서 **마지막 원소 카운트 누락** 버그

## 증상
클라이언트 오른쪽 상단의 **학습 PC LED** 가 로그인 후에도 회색(Unknown) 상태로 고정.
추론 PC #1, #2 LED 는 정상 반영. 학습서버 HEALTH_PONG 도 정상 수신되는데 오직
**학습 LED 만** 업데이트 안 됨.

## 원인 (Config 파서 버그)

`MainServer/src/core/config.cpp` 의 객체 배열 파싱 루프에서 **`idx++` 가
`consume(',')` 뒤에 있어 마지막 원소가 카운트에서 빠지는** 치명적 버그.

### 버그가 있던 루프
```cpp
while (p.peek() == '{') {
    p.consume('{');
    // ... 필드 파싱 → values_[targets.0.name] = "ai_inference_1" 등 ...
    p.consume('}');
    if (!p.consume(',')) break;   // ← 마지막 객체 뒤엔 ',' 없으니 break
    idx++;                        // ← break 된 뒤라 실행 안 됨 ★
}
values_[full_key + ".count"] = std::to_string(idx);
```

### 동작 재현
| 단계 | 상황 | values_ 저장 | idx |
|---|---|---|---|
| 1 | `{ai_inference_1}` | `targets.0.name = "ai_inference_1"` 등 | 0 |
| 1' | `consume(',')` 성공 | — | 1 |
| 2 | `{ai_inference_2}` | `targets.1.name = "ai_inference_2"` 등 | 1 |
| 2' | `consume(',')` 성공 | — | 2 |
| 3 | `{ai_training}` | `targets.2.name = "ai_training"` 등 | **2** |
| 3' | `consume(',')` 실패 (다음이 `]`) | break | **2** ← 증가 X |
| 4 | count 저장 | `targets.count = "2"` | 2 |

→ `targets.2.*` 데이터는 실제로 저장됐지만 **count=2 이라 `get_health_targets()` 가
`i < 2` 로 두 개만 반환** → `ai_training` 영원히 무시.

### 파급 효과 (원래 계획)
1. `GuiRouter` 의 초기 HEALTH_PUSH sync 루프가 2개만 순회 → 클라이언트에 `ai_training` push 안 감
2. MFC `OnNetHealthPush` 가 `ai_training` 을 한 번도 수신 못함
3. `m_sv0` (학습 PC LED) = `ServerState::Unknown` 고정 → 회색

## 수정

```cpp
while (p.peek() == '{') {
    p.consume('{');
    // ... 필드 파싱 ...
    p.consume('}');
    idx++;                        // ★ 먼저 증가
    if (!p.consume(',')) break;   // 그 다음 break 판정
}
values_[full_key + ".count"] = std::to_string(idx);
```

**변경 1 줄 이동 + 주석 6 줄**로 해결. 파싱 로직·값 자체는 불변.

## 범위 재확인 (다른 유사 패턴 없음)

- **문자열 배열 루프** (`config.cpp:207-210`):
  ```cpp
  while (p.peek() != ']') {
      arr.push_back(p.parse_string());  // 먼저 push
      if (!p.consume(',')) break;
  }
  ```
  `push_back` 이 앞에 있어 마지막 원소도 안전하게 저장. **버그 없음**.

- **MFC 측 JSON 배열 파서** (PageStation1, PageModel, PageHome):
  `rows.push_back(r)` 후 `pos = oe + 1` 구조. **버그 없음**.

- 영향받는 config.json 항목: **`health_check.targets` 뿐** (현재 프로젝트에서
  객체 배열은 여기만). 다른 설정은 문자열 배열(`allowed_ip_prefixes`) 이라 무관.

## 적용 방법

### 메인서버 **리빌드 필수**
```bash
cd /home/lms/Desktop/Factory/MainServer/build
cmake --build . -- -j$(nproc)
./factory_main_server
```

### 검증 로그
기동 후 클라이언트 로그인 시 다음 3줄이 **모두** 나와야 정상:
```
[CLT  ] 초기 HEALTH_PUSH 송신 | target=ai_inference_1 status=...
[CLT  ] 초기 HEALTH_PUSH 송신 | target=ai_inference_2 status=...
[CLT  ] 초기 HEALTH_PUSH 송신 | target=ai_training    status=...   ← 이 줄이 드디어 나옴
```

이전엔 첫 두 줄만 찍혔음 — 오늘 이전 로그에서 `ai_training` 문자열이 단 한 번도 등장 안 함이 확인됨 (기동 09:06 이후 수만 줄 중 0회).

### MFC 클라이언트
**재빌드 불필요** — 이미 `OnNetHealthPush` 가 `server_name.Find("train") >= 0` 로
`ai_training` 을 올바르게 처리하도록 되어있음. 메인서버만 고치면 즉시 LED 반영됨.

### AI 서버 (학습/추론)
**재빌드·재시작 불필요** — `HEALTH_PONG` 송신에 `server_type="training"` 이미 포함되어
있고 Router 의 `"training"` → `"ai_training"` 매핑 로직도 정상. 메인서버만 고치면 끝.

## 파급 재점검 — 이 버그가 일으킨 다른 문제들

1. ✅ **HealthChecker 의 주기 ping**:
   - 매 5초 tick 마다 `for (target : get_health_targets())` → 2개만 순회
   - 학습서버에 **HEALTH_PING 자체가 발송된 적 없음**
   - 학습서버가 "연결 상태"를 메인서버에 알릴 기회가 없었음

2. ✅ **Router 의 server_type 태깅**:
   - 학습서버가 다른 경로(TRAIN_COMPLETE 등)로 메시지 보낼 때만 `ai_training` 태깅
   - HEALTH_PONG 기반 sync 는 전혀 작동 안 함

3. ✅ **gui_notifier 의 health push**:
   - `SERVER_DOWN` / `SERVER_RECOVERED` 이벤트가 학습서버 대상으로 발생 안 함
   - 결과적으로 `server_name="ai_training"` 이 클라이언트로 송신된 적 없음

이 모두가 단 하나의 원인 — **배열 카운트 1 부족** — 으로부터 파생됨.

## 감사 포인트

- v0.11.0 도입 시 "동적 server_type 감지" 를 정교하게 구현했지만, **초기 HEALTH_PUSH
  sync 로직의 첫 시점**에서 배열 카운트가 2 면 `ai_training` 이 등장할 기회 자체가
  막혀 있었음.
- 로그에 `ai_training` 이 한 번도 등장 안 함이라는 사실이 결정적 단서.
- 문자열 배열과 객체 배열에서 `consume(',')` 와 후속 작업 순서가 일관되지 않았던
  게 뿌리. `push_back` 과 달리 `idx++` 는 "다음 순회" 를 위한 준비라 순서에 민감.

## 차기 정리 (선택)

- Config 파서에 **단위 테스트** 추가: `{...}, {...}, {...}` 같은 3원소 객체 배열 케이스
- `Config::validate_on_load()` 추가: `get_int("xxx.count")` 값과 실제 저장된 키 수를
  비교해 불일치 시 기동 에러
