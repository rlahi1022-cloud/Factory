// ============================================================================
// Factory_UI_CL.cpp — MFC 애플리케이션 진입점
// ============================================================================
// 목적:
//   프로그램의 시작점입니다. MFC 앱의 생명주기를 관리합니다.
//   InitInstance()에서 로그인 → 메인 윈도우 순서로 진행합니다.
//
// 실행 흐름:
//   1) Windows가 프로그램을 실행하면 MFC가 theApp 전역 객체 생성
//   2) MFC가 InitInstance() 호출
//   3) 로그인 다이얼로그 표시 → 인증 성공 시 메인 탭 다이얼로그 생성
//   4) 메인 메시지 루프 진입 (사용자 입력 + 네트워크 메시지 처리)
// ============================================================================

#include "pch.h"
#include "framework.h"
#include "Factory_UI_CL.h"
#include "LoginDlg.h"
#include "MainTabDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW  // 디버그 모드: 메모리 누수 추적용 new 오버라이드
#endif

// 메시지 맵: CFactoryUICLApp는 특별한 메시지를 처리하지 않음
BEGIN_MESSAGE_MAP(CFactoryUICLApp, CWinApp)
END_MESSAGE_MAP()

// ── 생성자 ───────────────────────────────────────────────────────────────
CFactoryUICLApp::CFactoryUICLApp()
{
    // Restart Manager 지원 (비정상 종료 시 자동 복구)
    m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;
}

// ── 전역 앱 객체 ────────────────────────────────────────────────────────
// MFC는 이 전역 객체를 통해 애플리케이션을 관리합니다.
// 프로그램당 정확히 1개만 존재해야 합니다.
CFactoryUICLApp theApp;

// ============================================================================
// InitInstance — 애플리케이션 초기화 (프로그램 시작 시 1회 호출)
// ============================================================================
// 반환값: TRUE=초기화 성공(메시지 루프 진입), FALSE=종료
BOOL CFactoryUICLApp::InitInstance()
{
    // ── 0단계: WinSock2 초기화 (프로그램 전체에서 1회만) ──
    // WSAStartup/WSACleanup은 프로세스 단위로 관리해야 합니다.
    // 여러 곳에서 호출하면 참조 카운트 불일치로 소켓이 무효화될 수 있습니다.
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // ── 1단계: 공용 컨트롤 초기화 ──
    // Windows 공용 컨트롤(ListView, TabCtrl 등)을 사용하기 위해 필요합니다.
    INITCOMMONCONTROLSEX InitCtrls;
    InitCtrls.dwSize = sizeof(InitCtrls);
    InitCtrls.dwICC  = ICC_WIN95_CLASSES     // 기본 컨트롤
                      | ICC_DATE_CLASSES      // DateTimePicker
                      | ICC_PROGRESS_CLASS    // ProgressBar
                      | ICC_TAB_CLASSES;      // TabCtrl
    InitCommonControlsEx(&InitCtrls);

    // MFC 기본 초기화
    CWinApp::InitInstance();
    // OLE 컨테이너 지원 (ActiveX 컨트롤 호스팅용)
    AfxEnableControlContainer();

    // ── 2단계: 로그인 다이얼로그 표시 ──
    // DoModal(): 모달 다이얼로그 표시 — 닫힐 때까지 대기
    // IDOK 반환 = 로그인 성공, IDCANCEL = 취소(프로그램 종료)
    CLoginDlg loginDlg;
    INT_PTR nResponse = loginDlg.DoModal();
    if (nResponse != IDOK)
        return FALSE;  // 로그인 취소 → 프로그램 종료

    // ── 3단계: 메인 탭 다이얼로그 생성 ──
    // 로그인 세션 정보를 메인 다이얼로그에 전달합니다.
    // m_pMainWnd: MFC가 관리하는 메인 윈도우 포인터
    CMainTabDlg* pDlg = new CMainTabDlg(loginDlg.m_session);
    m_pMainWnd = pDlg;
    pDlg->Create(IDD_MAIN_DLG, nullptr);     // 모달리스 다이얼로그로 생성
    pDlg->ShowWindow(SW_SHOWMAXIMIZED);       // 최대화 표시
    pDlg->UpdateWindow();                      // 즉시 그리기

    return TRUE;  // 메시지 루프 진입
}

// ============================================================================
// ExitInstance — 앱 종료 시 정리
// ============================================================================
int CFactoryUICLApp::ExitInstance()
{
    // WinSock2 정리 (InitInstance에서 1회 호출한 것과 쌍)
    WSACleanup();
    return CWinApp::ExitInstance();
}
