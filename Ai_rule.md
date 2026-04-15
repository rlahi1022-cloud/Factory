# AI 협업 규칙

본 프로젝트는 팀원 전원이 AI 도구(Claude / Copilot / Cursor 등)를 활용해 개발한다.
AI는 컨텍스트가 없으면 매번 합리적이지만 **다르게** 코드를 생성한다.
5명이 각자 AI를 쓰면서 일관성을 유지하기 위한 공통 규칙을 정의한다.

---

## 1. 프롬프트 시작 템플릿

새 작업을 시작할 때 AI에게 다음 4개 컨텍스트를 먼저 제공한다.

1. `Directory_README.md` — 명명 규칙, 폴더 구조
2. `Protocol_README.md` — 메시지 번호, ACK 정책
3. 작업 대상 파일의 **헤더** (인터페이스 계약)
4. `MainServer/include/EventTypes.h` 또는 `AiServer/Common/Protocol.py` (enum 정의)

### 예시 프롬프트

```
다음 4개 파일을 먼저 읽고, 우리 프로젝트의 컨벤션을 파악해줘.
1. Directory_README.md
2. Protocol_README.md
3. MainServer/include/StationHandler.h
4. MainServer/include/EventTypes.h

그 다음 Station1Handler::on_inspection() 안에서
defect_type이 "contamination"일 때만 추가 로깅하는 로직을 추가해줘.
명명 규칙(snake_case 변수/메소드)을 반드시 지켜줘.
```

---

## 2. 금지 사항

| 금지 | 이유 |
|------|------|
| **신규 메시지 번호 임의 생성** | 반드시 `Protocol.h` / `Protocol.py`에 먼저 추가 후 사용 |
| **외부 라이브러리 임의 도입** (nlohmann/json, boost 등) | 팀 합의 후 PR로 추가 |
| **명명 규칙 위반 코드 머지** | PascalCase 파일·클래스 / snake_case 변수·메소드 강제 |
| **`inspection_id` 포맷 변경** | `stationN-YYYYMMDDHHMMSSmmm-seq` 고정 |
| **`EventBus` 우회한 직접 함수 호출** | 컴포넌트 간 결합 분리를 위해 항상 `publish/subscribe` 통과 |
| **TCP `socket()` 직접 호출** (Python) | 반드시 `TcpClient.send_with_ack` / `send_fire_and_forget` 사용 |
| **매직 넘버 사용** | `1000` 대신 `int(ProtocolNo.STATION1_NG)` |
| **DB 접속정보 하드코딩** | `Main.cpp` 단일 위치에서만 설정 (Config 분리 예정) |

---

## 3. 권장 사항

### 3.1 코드 작성 시 AI에게 명시할 것

```
- "기존 EventBus.subscribe 패턴을 따라 핸들러를 등록해줘"
- "TCP 송신은 TcpClient.send_with_ack를 사용해. 직접 socket을 만들지 마"
- "새 이벤트가 필요하면 먼저 EventTypes.h의 EventType enum에 추가해줘"
- "메시지 번호는 Protocol.h의 ProtocolNo enum 값을 써. 매직 넘버 금지"
- "외부 라이브러리는 추가하지 말고, 표준 라이브러리만 사용해줘"
```

### 3.2 새 기능 추가 워크플로우

1. **Protocol 먼저** — 새 메시지가 필요하면 `Protocol.h` / `Protocol.py`에 enum 추가 (양쪽 동시)
2. **EventTypes 먼저** — 새 이벤트가 필요하면 `EventTypes.h`에 `EventType` + 페이로드 struct 추가
3. **헤더 먼저** — 클래스 인터페이스(`*.h`) 작성 후 구현
4. **테스트 코드 동반** — 기능 1개 추가 시 최소 1개의 검증 시나리오

### 3.3 AI에게 작업 분할 요청

대형 작업은 한 번에 시키지 말고 단계별로 나눈다.

```
1단계: Protocol.h에 STATION1_NG_RETRANS=1005 추가만 해줘
2단계: 그 enum을 사용하는 송신 함수 골격만 만들어줘
3단계: 송신 후 ACK 대기 로직 추가해줘
```

각 단계 결과를 사람이 검토 후 다음 단계 진행.

---

## 4. 코드 리뷰 체크리스트 (AI 생성 코드)

PR 생성 전 본인이 셀프 체크하고, 리뷰어도 같은 항목을 확인한다.

- [ ] **명명 규칙** 준수 (파일·클래스 PascalCase / 변수·메소드 snake_case)
- [ ] `Protocol_README.md`의 메시지 번호와 enum 값 일치
- [ ] `EventTypes.h` / `Protocol.py`의 enum 사용 (매직 넘버 없음)
- [ ] `inspection_id` 포맷 준수 (`stationN-YYYYMMDDHHMMSSmmm-seq`)
- [ ] 외부 라이브러리 추가 시 팀 슬랙 공지 + PR description에 명시
- [ ] `EventBus` / `TcpClient` 등 공용 컴포넌트 우회 없음
- [ ] 새 메시지 번호 추가 시 C++ `Protocol.h`와 Python `Protocol.py` 양쪽 동기화
- [ ] AI가 만든 코드의 모든 줄을 본인이 **이해**함 (질문 받으면 설명 가능)

---

## 5. AI 사용 한계 — "AI에게 맡기지 말 것"

다음 항목은 AI 생성 코드를 그대로 쓰지 않고 **사람이 결정**한다.

| 항목 | 이유 |
|------|------|
| **메시지 번호 부여** | 요구사항 분석서와 정합성 필요 |
| **DB 스키마 변경** | 모든 팀원 영향, 회의 안건 |
| **timeout / retry 횟수** | 시스템 안정성 직결, 합의된 값(1초/3회) 변경 시 회의 |
| **포트 번호 변경** | 9000 / 9101 / 9102 / 9201 합의됨 |
| **외부 라이브러리 도입** | 빌드 환경 영향 |
| **EventType / ProtocolNo enum 항목 삭제·번호 변경** | 양 서버 동시 변경 필요 |

---

## 6. 트러블슈팅 — AI가 자주 일으키는 실수

| 증상 | 원인 | 해결 |
|------|------|------|
| 변수가 `stationId`로 생성됨 | AI 기본 컨벤션은 camelCase | 프롬프트에 "snake_case 강제" 명시 |
| `nlohmann/json` 등 외부 라이브러리 추가 | AI는 편의 라이브러리 선호 | "표준 라이브러리만" 명시 |
| 메시지 번호를 임의로 999, 2000 등 생성 | enum 미참조 | `Protocol.h` 먼저 첨부 |
| Python에서 `socket.socket()` 직접 사용 | `TcpClient` 존재 모름 | `TcpClient.py` 먼저 첨부 |
| 같은 기능이 여러 파일에 중복 생성 | 디렉터리 구조 모름 | `Directory_README.md` 먼저 첨부 |
| ACK 타임아웃을 5초로 설정 | 합의값 모름 | `Protocol_README.md` 먼저 첨부 |

---

## 7. 일일 체크 (개발 기간 동안)

매일 종료 시 5분 투자.

- [ ] 오늘 추가된 메시지 번호가 `Protocol.h`와 `Protocol.py` 양쪽에 있는가
- [ ] 오늘 만든 파일이 `Directory_README.md`의 구조에 부합하는가
- [ ] 오늘 AI가 만든 코드 중 본인이 설명 못 하는 줄이 있는가 → 있으면 다시 학습
- [ ] README와 실제 코드 상태가 어긋나는 항목이 새로 생겼는가
- [ ] 오늘 작성한 코드가 기존 구조 (EventBus / Handler 흐름)를 벗어나지 않았는가
