# v0.15.7 — Station1 NG 이력 리스트 YOLO 컬럼 숨김

## 배경
`CPageStation1` 의 하단 NG 이력 리스트(`CNgHistoryList`)에 "클래스 / 신뢰도 / 판정"
3컬럼이 있지만, Station1 은 **PatchCore 이상탐지** 모델이라 YOLO 객체 탐지의 "클래스"
개념이 없음. 해당 컬럼이 항상 `-` 로만 표시되어 사용자 혼동 유발.

## 수정

### `CNgHistoryList` 에 YOLO 컬럼 표시 플래그 추가

```cpp
// CameraView.h
class CNgHistoryList : public CStatic {
public:
    void SetYoloColumnsVisible(bool visible) {
        m_showYoloCols = visible;
        if (GetSafeHwnd()) Invalidate(FALSE);
    }
protected:
    bool m_showYoloCols = true;   // 기본 true → 기존 사용처 영향 없음
};
```

### `DrawHeader` / `DrawRow` 에서 플래그 분기

```cpp
// v0.15.7: m_showYoloCols=false 면 YOLO 3컬럼 전부 생략
if (m_showYoloCols) {
    cell(_T("클래스"),   kColDetClsW);
    cell(_T("신뢰도"),   kColDetConfW);
    cell(_T("판정"),     kColDetOkW);
}
```

### `PageStation1::OnInitDialog` 에서 플래그 끄기

```cpp
// v0.15.7: Station1 은 PatchCore — YOLO 컬럼 의미 없음
m_ngList.SetYoloColumnsVisible(false);
```

## 영향 범위

| 페이지 | `m_showYoloCols` | NG 이력 컬럼 |
|---|---|---|
| **Station1** | `false` | ID / 스테이션 / 시각 / 결과 / 점수 (5개) |
| **Station2** | `true` (기본값) | 위 + **클래스 / 신뢰도 / 판정** (8개) |
| **PageHome (종합현황)** | `true` (기본값) | 8개 모두 (혼합 이력 표시) |

Station2 의 상단 `m_listYolo` (별도 YOLO 디텍션 리스트) 는 변경 없음 — 그대로 3컬럼 표시.

## 검증

MFC 리빌드 후:
1. 입고 검사 탭 → NG 이력 헤더에 "ID / 스테이션 / 시각 / 결과 / 점수" 5컬럼만 보임
2. 조립 검사 탭 → 기존 8컬럼 그대로 (클래스=cap/label/liquid_level, 신뢰도 0.XX, 판정=OK/NG)
3. 종합 현황 탭 → 8컬럼 그대로

## 빌드

MFC 클라이언트 Clean → Rebuild. 메인/AI 서버 재빌드 불필요.
