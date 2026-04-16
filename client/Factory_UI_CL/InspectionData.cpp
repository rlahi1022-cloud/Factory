// ============================================================================
// InspectionData.cpp — 검사 데이터 유틸리티 구현부
// ============================================================================
// 목적:
//   결함 이름 변환, 시뮬레이션 데이터 생성, UI 색상 상수를 제공합니다.
//   서버 미연결 시에도 UI가 동작하도록 시뮬레이션 데이터를 생성합니다.
// ============================================================================

#include "pch.h"
#include "InspectionData.h"

namespace QCUtil {

// DefectName: 결함 열거형 → 한글 표시명 변환
// 파라미터: d — 결함 유형 열거값
// 반환값: 한글 결함 이름 문자열
CString DefectName(EDefect d) {
    switch (d) {
    case EDefect::Anomaly:    return _T("이상 감지");
    case EDefect::CapLoose:   return _T("캡 미체결");
    case EDefect::CapMissing: return _T("캡 없음");
    case EDefect::LabelTilt:  return _T("라벨 기울어짐");
    case EDefect::LabelTorn:  return _T("라벨 찢어짐");
    case EDefect::FillLow:    return _T("미충전");
    default:                  return _T("-");
    }
}

// GenRecord: 시뮬레이션용 랜덤 검사 레코드 1건 생성
// 파라미터: nextId — 부여할 검사 ID
// 반환값: 랜덤 생성된 검사 레코드
// 동작: 8% 확률로 NG, 나머지는 OK. Station은 1/2 랜덤 배정.
InspectionRecord GenRecord(int nextId) {
    InspectionRecord r;
    r.id      = nextId;
    r.station = (rand() % 2) + 1;         // 1(입고) 또는 2(조립) 랜덤
    r.isNG    = (rand() % 100) < 8;       // 8% 확률로 불량
    SYSTEMTIME st; GetLocalTime(&st);
    r.time.Format(_T("%02d:%02d:%02d"), st.wHour, st.wMinute, st.wSecond);
    if (r.isNG) {
        r.score  = 0.60 + (rand() % 35) / 100.0;
        if (r.station == 1) r.defect = EDefect::Anomaly;
        else {
            static EDefect pool[] = { EDefect::CapLoose, EDefect::CapMissing,
                EDefect::LabelTilt, EDefect::LabelTorn, EDefect::FillLow };
            r.defect = pool[rand() % 5];
        }
    } else {
        r.score  = (rand() % 30) / 100.0;
        r.defect = EDefect::None;
    }
    r.latencyMs = 40 + rand() % 80;
    return r;
}

// GenInitialHistory: 프로그램 시작 시 초기 이력 20건 생성
// srand: 난수 시드를 현재 시각으로 초기화 (매번 다른 데이터 생성)
std::vector<InspectionRecord> GenInitialHistory() {
    srand((unsigned)time(nullptr));
    std::vector<InspectionRecord> v;
    for (int i = 0; i < 20; ++i) v.push_back(GenRecord(10000 + i));
    return v;
}

// ── 색상 유틸리티 ─────────────────────────────────────────────────────────
// MFC 클래식 UI 스타일에 맞는 색상값을 반환합니다.

COLORREF ColOK()        { return RGB(0,128,0); }
COLORREF ColNG()        { return RGB(204,0,0); }
COLORREF ColWarn()      { return RGB(255,140,0); }
COLORREF ColHighlight() { return RGB(49,106,197); }
COLORREF ColBorder()    { return RGB(128,128,128); }
COLORREF ColCtrl()      { return RGB(236,233,216); }
COLORREF ColFace()      { return RGB(240,237,228); }

} // namespace QCUtil
