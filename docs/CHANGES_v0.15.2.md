# v0.15.2 — PageStats (통계/이력) 탭 완전 제거

## 배경
admin 브랜치의 선행 커밋 `5c821f3 통계 제거 필요` (PageStats 내부 로직 축소)
를 이어받아, **통계/이력 탭 UI 자체를 프로젝트에서 걷어낸 마무리 작업**.

5개 탭 구성(종합 현황 / ①입고 / ②조립 / 통계·이력 / 모델 관리) 에서
**통계·이력 탭을 제거해 4개 탭 구성**으로 단순화. 기존 Summary 기능은
PageHome 이 계속 담당하므로 사용자 가시 정보 손실은 거의 없음.

---

## 변경 파일

| 분류 | 파일 | 변경 성격 |
|---|---|---|
| 삭제 | `client/Factory_UI_CL/PageStats.cpp` | 235줄 파일 완전 삭제 |
| 삭제 | `client/Factory_UI_CL/PageStats.h` | 67줄 파일 완전 삭제 |
| 수정 | `client/Factory_UI_CL/MainTabDlg.h` | `#include "PageStats.h"`, `m_stats` 멤버 삭제 |
| 수정 | `client/Factory_UI_CL/MainTabDlg.cpp` | `m_stats` 참조 7곳 모두 제거 |
| 수정 | `client/Factory_UI_CL/Factory_UI_CL.vcxproj` | PageStats.cpp/h 엔트리 제거 |
| 수정 | `client/Factory_UI_CL/Factory_UI_CL.vcxproj.filters` | PageStats.cpp/h 엔트리 제거 |

**변경 규모**: `6 files, +27 / -334 lines` (순감소 307줄)

---

## MainTabDlg 에서 제거된 `m_stats` 참조 7곳

| 위치 | 이전 역할 | 처리 |
|---|---|---|
| 멤버 선언 | 탭 3: 통계/이력 페이지 홀더 | 삭제 |
| `SetNetworkClient` 주입 | PageStats 에 network 핸들 | 삭제 |
| `create_safe("Stats", …)` | 페이지 생성 | 삭제 |
| `show(m_stats.get(), 3)` | 탭 전환 시 표시 | 삭제 |
| `mv(m_stats.get())` | 레이아웃 적용 | 삭제 |
| `Update(recs_copy)` | 실시간 레코드 전달 | 삭제 |
| `OnInspectHistoryRes` | DB 이력 응답 수신 | **PageHome + PageStation1 이 분담하여 유지** |
| `OnStatsRes` | 통계 응답 수신 | **PageHome::ApplyStatsRes 가 Summary 초기화용으로 단독 처리** |
| `LookupInspectionMeta` | NG_IMAGE 수신 시 timestamp/score 조회 | **패킷(pkt->timestamp_iso / pkt->score) 에서 직접 추출 (v0.14.7 이미 담김)** |

## 탭 인덱스 조정

| 이전 | 탭 이름 | 이후 |
|---|---|---|
| 0 | 종합 현황 | 0 (유지) |
| 1 | ① 입고 검사 | 1 (유지) |
| 2 | ② 조립 검사 | 2 (유지) |
| 3 | ~~통계/이력~~ | — **삭제** |
| 4 | 모델 관리 | **3** (승격) |

---

## 유지한 항목 (의도적)

| 항목 | 사유 |
|---|---|
| `STATS_REQ(130)` / `STATS_RES(131)` 프로토콜 | `MainTabDlg` 가 로그인 직후 송신 → `PageHome::ApplyStatsRes` 가 Summary 누적값 초기화 |
| `INSPECT_HISTORY_REQ(114)` / `_RES(115)` | `PageHome` NG 리스트 + `PageStation1::PopulateNgHistoryFromJson` 에서 계속 사용 |
| MainServer `gui_router::handle_stats`, `StatsDao::get_stats` / `get_history` | 프로토콜 응답자 — **변경 불필요**, 메인서버 재빌드 불요 |
| `Resource.h` 의 `IDD_PAGE_STATS` 상수 | 참조 없이 남겨둠 (빌드 안정성, 차후 일괄 청소 대상) |
| `FactoryUICL.rc` 의 IDD_PAGE_STATS 다이얼로그 리소스 | 동일 — 참조 없으면 EXE 크기만 소폭 증가할 뿐 실행 영향 없음 |

## MainServer / AiServer 영향

**코드·재빌드·재시작 모두 불필요**. 클라이언트 UI 에서만 탭 하나가 사라진 것.
- `STATS_REQ` 는 여전히 클라에서 와서 메인서버가 응답
- `INSPECT_HISTORY_REQ` 응답 DAO 도 그대로
- AI 서버와는 무관한 변경

---

## 커밋 / 반영 브랜치

| 커밋 | 내용 |
|---|---|
| `5c821f3` (admin) | 통계 제거 필요 — PageStats 내부 로직 축소 |
| `dccff65` (feat/hy) | `Merge 'feat/admin' into feat/hy` |
| `94a1e75` (feat/hy / develop) | **v0.15.2 PageStats 탭 완전 제거** |

원격 반영 완료: `origin/develop`, `origin/feat/hy` 모두 `94a1e75` 동기화.

---

## 검증 체크리스트 (리빌드 후)

- [ ] Visual Studio Clean → Rebuild — 컴파일 에러 없는지
- [ ] 앱 실행 → 로그인 후 탭 바에 "종합 현황 / ① 입고 검사 / ② 조립 검사 / 모델 관리" **4개** 표시
- [ ] 종합 현황 탭의 Summary (Total/OK/NG/DefectRate) 값이 로그인 직후 서버 실제 수치로 채워지는지 (`STATS_RES` 정상 수신)
- [ ] 홈/Station1 의 NG 이력 리스트가 로그인 직후 DB 값으로 채워지는지 (`INSPECT_HISTORY_RES` 정상 수신)
- [ ] NG 푸시 실시간 수신 시 상단 3뷰 + 하단 리스트 동작
- [ ] 모델 관리 탭이 이전 "탭 인덱스 4" 가 아닌 "탭 인덱스 3" 에서 정상 표시

## 차기 정리 후보 (선택)

- `Resource.h` 에서 `IDD_PAGE_STATS`, `IDC_LIST_RECS`, `IDC_CHART_PARETO` 등 통계 관련 IDC 상수 정리
- `FactoryUICL.rc` 의 `IDD_PAGE_STATS` 다이얼로그 블록 + 관련 스트링 제거
- 이 두 작업은 빌드/실행에 영향 없이 선택적으로 가능 — v0.15.3 hotfix 후보
