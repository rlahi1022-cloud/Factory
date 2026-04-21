//{{NO_DEPENDENCIES}}
// Microsoft Visual C++에서 생성한 포함 파일입니다.
#pragma once

// ── 기존 VS 자동생성 ID (삭제하면 안 됨) ─────────────────────────────────
#define IDR_MAINFRAME                   128
#define IDM_ABOUTBOX                    0x0010
#define IDD_ABOUTBOX                    100
#define IDS_ABOUTBOX                    101
#define IDD_FACTORY_UI_CL_DIALOG        102

// ── 메뉴 ──────────────────────────────────────────────────────────────────
#define IDR_MAINMENU                    300
#define ID_FILE_EXIT                    301
#define ID_FILE_LOGOUT                  306     // 로그아웃
#define ID_VIEW_REFRESH                 302
#define ID_INSPECT_START                303
#define ID_INSPECT_STOP                 304
#define ID_HELP_ABOUT                   305

// ── 타이머 ────────────────────────────────────────────────────────────────
#define IDT_LIVE_UPDATE                 400
#define IDT_STATUSBAR                   401
#define IDT_TRAINING                    402
#define IDT_CAM_FLASH                   403

// ── 다이얼로그 ────────────────────────────────────────────────────────────
#define IDD_LOGIN_DLG                   200
#define IDD_MAIN_DLG                    201
#define IDD_PAGE_HOME                   210
#define IDD_PAGE_STATION1               211
#define IDD_PAGE_STATION2               212
#define IDD_PAGE_STATS                  213
#define IDD_PAGE_MODEL                  214

// ── 로그인 컨트롤 ─────────────────────────────────────────────────────────
#define IDC_EDIT_USERNAME               1001
#define IDC_EDIT_PASSWORD               1002
#define IDC_STATIC_ERROR                1003
#define IDC_BTN_SWITCH_MODE             1004
#define IDC_EDIT_EMPID                  1005
#define IDC_EDIT_PASS_CONFIRM           1006
#define IDC_COMBO_ROLE                  1007

// ── 메인 탭 ───────────────────────────────────────────────────────────────
#define IDC_MAIN_TAB                    2000

// ── 홈 페이지 ─────────────────────────────────────────────────────────────
#define IDC_STATIC_TOTAL                2100
#define IDC_STATIC_OK                   2101
#define IDC_STATIC_NG                   2102
#define IDC_STATIC_DEFECT_RATE          2103
#define IDC_STATIC_UPTIME               2104
#define IDC_LIST_NG                     2105

// ── 스테이션 1 ────────────────────────────────────────────────────────────
#define IDC_CAM1_VIEW                   2200
#define IDC_HEATMAP1_VIEW               2201
#define IDC_PREDMASK1_VIEW              2214    // Pred Mask 뷰 (3번째 패널)
#define IDC_NG_LIST1                    2215    // NG 이벤트 이력 세로 리스트 (스테이션1)
#define IDC_STATIC_S1_RESULT            2202
#define IDC_STATIC_S1_SCORE             2203
#define IDC_BTN_S1_OK                   2204
#define IDC_BTN_S1_NG                   2205
#define IDC_BTN_S1_ARDUINO              2206
#define IDC_STATIC_S1_LED               2207

// ── 스테이션 2 ────────────────────────────────────────────────────────────
#define IDC_CAM2_VIEW                   2300
#define IDC_HEATMAP2_VIEW               2301
#define IDC_LIST_YOLO                   2302
#define IDC_STATIC_S2_RESULT            2303
#define IDC_STATIC_S2_SCORE             2304
#define IDC_BTN_S2_DEFECT               2305
#define IDC_BTN_S2_REWORK               2306
#define IDC_STATIC_S2_LED               2307

// ── 통계 페이지 ───────────────────────────────────────────────────────────
#define IDC_DATE_FROM                   2400
#define IDC_DATE_TO                     2401
#define IDC_COMBO_STATION_FILTER        2402
#define IDC_BTN_QUERY                   2403
#define IDC_BTN_EXPORT_CSV              2404

// ── 모델 페이지 ───────────────────────────────────────────────────────────
#define IDC_LIST_MODELS                 2500
#define IDC_COMBO_TARGET                2501
#define IDC_EDIT_PRODUCT_NAME           2502
#define IDC_BTN_SELECT_FOLDER           2503
#define IDC_BTN_RETRAIN                 2504
#define IDC_PROGRESS_TRAINING           2505
#define IDC_STATIC_TRAIN_STATUS         2506
#define IDC_LIST_UPLOADED               2507
#define IDC_BTN_CLEAR_FILES             2508

// ── 홈 페이지 추가 (입고/조립 별도 집계) ──────────────────────────────────
#define IDC_STATIC_S1_OK                2110    // ① 입고 OK 수
#define IDC_STATIC_S1_NG                2111    // ① 입고 NG 수
#define IDC_STATIC_S2_OK                2112    // ② 조립 OK 수
#define IDC_STATIC_S2_NG                2113    // ② 조립 NG 수
#define IDC_STATIC_S1_MODEL_INFO        2114    // ① 입고 모델 정보
#define IDC_STATIC_S2_MODEL_INFO        2115    // ② 조립 모델 정보

// ── 스테이션1 추가 (검사 설정 정보) ───────────────────────────────────────
#define IDC_STATIC_S1_CFG_MODEL         2210    // 모델명
#define IDC_STATIC_S1_CFG_INPUT         2211    // 입력 크기
#define IDC_STATIC_S1_CFG_THRESH        2212    // NG 임계값
#define IDC_STATIC_S1_CFG_BACKBONE      2213    // 백본

// ── 모델 페이지 추가 (학습 PC 정보) ──────────────────────────────────────
#define IDC_STATIC_TRAIN_SERVER         2510    // 서버 OS
#define IDC_STATIC_TRAIN_GPU            2511    // GPU 정보
#define IDC_STATIC_TRAIN_FRAMEWORK      2512    // 프레임워크

// ── 네트워크 타이머 ──────────────────────────────────────────────────────
#define IDT_RECONNECT                   410     // 서버 재접속 타이머

// ── 메뉴 추가 (네트워크) ─────────────────────────────────────────────────
#define ID_NET_CONNECT                  310     // 서버 접속
#define ID_NET_DISCONNECT               311     // 서버 접속 해제

#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE        310
#define _APS_NEXT_CONTROL_VALUE         3000
#define _APS_NEXT_SYMED_VALUE           101
#define _APS_NEXT_COMMAND_VALUE         32771
#endif
#endif

