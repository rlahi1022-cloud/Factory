// ─────────────────────────────────────────────────────────────────────────────
// resource.h 에 추가할 ID 정의 (IDD_PAGE_STATION2 v0.13 신규 컨트롤)
// 기존 ID 와 충돌하지 않는 범위(예: 3100~3199)에 배정.
// 실제 프로젝트의 resource.h 에서 빈 범위를 확인 후 조정하세요.
// ─────────────────────────────────────────────────────────────────────────────

// ── [신규] Pred Mask 뷰 ────────────────────────────────────────────────────
#define IDC_PREDMASK2_VIEW          3100

// ── [신규] 레이블 ─────────────────────────────────────────────────────────
#define IDC_STATIC_S2_LBL_HEAT      3101   // "Anomaly Heatmap" 레이블
#define IDC_STATIC_S2_LBL_MASK      3102   // "Pred Mask" 레이블

// ── [신규] Final Result 패널 (우측 패널용 — 기존 IDC_STATIC_S2_RESULT 와 구분)
#define IDC_STATIC_S2_RESULT_PANEL  3103

// ── [신규] 카테고리 필터 버튼 ─────────────────────────────────────────────
#define IDC_BTN_S2_CAT_ALL          3110   // "전체" 버튼
#define IDC_BTN_S2_CAT_YOLO         3111   // "YOLO" 버튼
#define IDC_BTN_S2_CAT_PC           3112   // "PatchCore" 버튼
#define IDC_STATIC_S2_CAT_DESC      3113   // 카테고리 설명 레이블

// ── [신규] NG 이벤트 이력 리스트 ──────────────────────────────────────────
#define IDC_NG_LIST2                3120   // CListCtrl (Report 모드)
#define IDC_STATIC_S2_NG_COUNT      3121   // "금일 N건" 레이블

// ── [신규] 선택된 NG 상세 이미지 ──────────────────────────────────────────
#define IDC_STATIC_S2_NG_TITLE      3130   // 상세 헤더 텍스트
#define IDC_NG2_IMG0                3131   // 이미지 (원본 or YOLO or PC)
#define IDC_NG2_IMG1                3132   // Heatmap
#define IDC_NG2_IMG2                3133   // Pred Mask

// ── [신규] 아두이노 상세 상태 필드 ───────────────────────────────────────
#define IDC_STATIC_S2_SERIAL        3140   // "● OK (COM5)"
#define IDC_STATIC_S2_LAST          3141   // 마지막 신호 시각
#define IDC_STATIC_S2_SOL           3142   // Solenoid ON/OFF
#define IDC_STATIC_S2_SENS          3143   // Sensor Ready/Fault
// IDC_STATIC_S2_LED 은 기존 유지 (상태 "대기중"/"검사중")
