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

// v0.14.6: GenRecord / GenInitialHistory 제거 — 시뮬레이션 데이터 생성 기능 삭제.
//   실서버 데이터만 UI 에 반영되도록 정리. 헤더(InspectionData.h) 선언도 함께 제거.

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
