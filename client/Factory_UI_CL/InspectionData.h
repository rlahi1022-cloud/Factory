// ============================================================================
// InspectionData.h — 검사 데이터 구조체 및 유틸리티 정의
// ============================================================================
// 목적:
//   프로그램 전역에서 사용되는 핵심 데이터 구조체와
//   시뮬레이션 데이터 생성, 색상 유틸리티를 제공합니다.
// ============================================================================

#pragma once
#include "pch.h"
#include <vector>

// ── 결함 유형 열거형 ─────────────────────────────────────────────────────
// Station1(입고): Anomaly만 사용 (PatchCore는 결함 유형 분류 불가)
// Station2(조립): CapLoose~FillLow 사용 (YOLO11이 구체적 결함 분류)
enum class EDefect { None=0, Anomaly, CapLoose, CapMissing, LabelTilt, LabelTorn, FillLow };

// ── 검사 결과 레코드 ─────────────────────────────────────────────────────
// 한 건의 검사 결과를 담는 구조체 (서버 수신 또는 시뮬레이션 생성)
struct InspectionRecord {
    int     id;          // 검사 고유 ID
    int     station;     // 스테이션 번호 (1=입고, 2=조립)
    CString time;        // 검사 시각 ("HH:MM:SS")
    bool    isNG;        // 판정 결과 (true=불량)
    double  score;       // 이상 점수 (0.0~1.0)
    EDefect defect;      // 결함 유형
    int     latencyMs;   // 추론 소요 시간 (ms)
};

// ── 사용자 세션 정보 ─────────────────────────────────────────────────────
struct UserSession {
    CString username;    // 사용자 이름
    CString role;        // 권한 등급 ("Admin", "Operator", "Viewer")
    CString employeeId;  // 사원 ID
};

// ── QCUtil — 유틸리티 함수 모음 ──────────────────────────────────────────
namespace QCUtil {
    CString DefectName(EDefect d);                          // 결함 → 한글 이름
    InspectionRecord GenRecord(int nextId);                  // 시뮬레이션 레코드 1건 생성
    std::vector<InspectionRecord> GenInitialHistory();       // 초기 이력 20건 생성
    COLORREF ColOK();         // 정상 초록: (0,128,0)
    COLORREF ColNG();         // 불량 빨강: (204,0,0)
    COLORREF ColWarn();       // 경고 주황: (255,140,0)
    COLORREF ColHighlight();  // 강조 파랑: (49,106,197)
    COLORREF ColBorder();     // 테두리 회색
    COLORREF ColCtrl();       // 컨트롤 배경
    COLORREF ColFace();       // 면 배경
}
