// ============================================================================
// LoginDlg.cpp — 로그인/회원가입 다이얼로그
// ============================================================================
// 책임:
//   앱 최초 기동 시 표시되는 창. 서버 접속 → 인증 → MainTabDlg 전환의
//   게이트웨이 역할.
//
// 프로토콜:
//   100 LOGIN_REQ      / 101 LOGIN_RES
//   104 REGISTER_REQ   / 105 REGISTER_RES
//
// 모드 전환:
//   OnBtnSwitch 로 로그인/회원가입 모드를 같은 다이얼로그에서 토글.
//   컨트롤 가시성만 바꿔 재사용 → 화면 전환 UX 간소화.
//
// 서버 연결 수명:
//   이 창에서 먼저 TCP 접속 후 로그인 시도. 성공 시 MainTabDlg 로
//   소유권 이전(CNetworkClient 객체 전달) → 창 닫혀도 연결 유지.
//   실패/취소 시 Disconnect 후 앱 종료.
//
// 예외/재연결:
//   WM_NET_DISCONNECTED 수신 시 에러 메시지 표시 + 입력 필드 리셋.
// ============================================================================

#include "pch.h"
#include "LoginDlg.h"
#include "PacketBuilder.h"
#include "ClientConfig.h"

// ── RTTI + 메시지 맵 ─────────────────────────────────────────────────────
IMPLEMENT_DYNAMIC(CLoginDlg, CDialogEx)

BEGIN_MESSAGE_MAP(CLoginDlg, CDialogEx)
    ON_BN_CLICKED(IDOK,                OnBtnOK)
    ON_BN_CLICKED(IDCANCEL,            OnBtnCancel)
    ON_BN_CLICKED(IDC_BTN_SWITCH_MODE, OnBtnSwitch)
    ON_MESSAGE(WM_NET_LOGIN_RES,       OnLoginRes)
    ON_MESSAGE(WM_NET_REGISTER_RES,    OnRegisterRes)
    ON_MESSAGE(WM_NET_DISCONNECTED,    OnNetDisconnected)
    ON_WM_PAINT()
END_MESSAGE_MAP()

// ============================================================================
// 생성자
// ============================================================================
CLoginDlg::CLoginDlg(CWnd* p)
    : CDialogEx(IDD_LOGIN_DLG, p)
    , m_regMode(false)
    , m_waitingResponse(false)
{
}

// ============================================================================
// DoDataExchange — 컨트롤 ↔ 변수 연결 (DDX)
// ============================================================================
void CLoginDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_USERNAME,     m_edUser);
    DDX_Control(pDX, IDC_EDIT_PASSWORD,     m_edPass);
    DDX_Control(pDX, IDC_EDIT_EMPID,        m_edEmpId);
    DDX_Control(pDX, IDC_EDIT_PASS_CONFIRM, m_edPassConfirm);
    DDX_Control(pDX, IDC_COMBO_ROLE,        m_cbRole);
    DDX_Control(pDX, IDC_STATIC_ERROR,      m_stErr);
}

// ============================================================================
// OnInitDialog — 초기화
// ============================================================================
BOOL CLoginDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetWindowText(_T("Factory QC — 사용자 인증"));

    m_edUser.SetWindowText(_T(""));

    // 권한 등급 콤보박스 항목 추가
    m_cbRole.AddString(_T("Viewer (조회 전용)"));
    m_cbRole.AddString(_T("Operator (검사 운영)"));
    m_cbRole.AddString(_T("Admin (전체 관리)"));
    m_cbRole.SetCurSel(1);  // 기본 선택: Operator

    // 로그인 모드로 시작
    SwitchMode(false);

    return TRUE;
}

// ============================================================================
// SwitchMode — 로그인 ↔ 회원가입 모드 전환
// ============================================================================
void CLoginDlg::SwitchMode(bool reg)
{
    m_regMode = reg;
    m_stErr.SetWindowText(_T(""));

    int showReg  = reg ? SW_SHOW : SW_HIDE;
    int showLogin = reg ? SW_HIDE : SW_SHOW;

    if (GetDlgItem(IDC_EDIT_EMPID))        GetDlgItem(IDC_EDIT_EMPID)->ShowWindow(showReg);
    if (GetDlgItem(IDC_EDIT_PASS_CONFIRM)) GetDlgItem(IDC_EDIT_PASS_CONFIRM)->ShowWindow(showReg);
    if (GetDlgItem(IDC_COMBO_ROLE))        GetDlgItem(IDC_COMBO_ROLE)->ShowWindow(showReg);
    if (GetDlgItem(1008))                  GetDlgItem(1008)->ShowWindow(showReg);
    if (GetDlgItem(1009))                  GetDlgItem(1009)->ShowWindow(showReg);
    if (GetDlgItem(1010))                  GetDlgItem(1010)->ShowWindow(showReg);

    if (GetDlgItem(IDCANCEL)) GetDlgItem(IDCANCEL)->ShowWindow(showLogin);

    GetDlgItem(IDC_BTN_SWITCH_MODE)->SetWindowText(
        reg ? _T("← 로그인") : _T("회원가입 →"));
    GetDlgItem(IDOK)->SetWindowText(reg ? _T("가입") : _T("확인"));
    SetWindowText(reg ? _T("Factory QC — 회원가입") : _T("Factory QC — 사용자 인증"));
}

// ============================================================================
// SetError — 에러 메시지 표시
// ============================================================================
void CLoginDlg::SetError(LPCTSTR msg)
{
    m_stErr.SetWindowText(msg);
}

// ============================================================================
// OnBtnOK — 확인(로그인) / 가입 버튼 클릭
// ============================================================================
void CLoginDlg::OnBtnOK()
{
    if (m_waitingResponse) return;  // 서버 응답 대기 중이면 무시

    CString user, pass;
    m_edUser.GetWindowText(user);
    m_edPass.GetWindowText(pass);

    if (m_regMode) {
        // ── 회원가입 처리 ──
        CString empId, passConfirm;
        m_edEmpId.GetWindowText(empId);
        m_edPassConfirm.GetWindowText(passConfirm);

        if (empId.IsEmpty())    { SetError(_T("사원 ID를 입력하세요.")); return; }
        if (user.IsEmpty())     { SetError(_T("사용자 이름을 입력하세요.")); return; }
        if (pass.GetLength()<4) { SetError(_T("암호는 4자 이상이어야 합니다.")); return; }
        if (pass != passConfirm){ SetError(_T("암호가 일치하지 않습니다.")); return; }

        // 권한 등급 텍스트 변환
        CString roleDisplay;
        m_cbRole.GetWindowText(roleDisplay);
        CString role;
        if (roleDisplay.Find(_T("Admin")) >= 0)         role = _T("Admin");
        else if (roleDisplay.Find(_T("Operator")) >= 0) role = _T("Operator");
        else                                              role = _T("Viewer");

        // 서버에 연결하여 REGISTER_REQ 전송
        m_loginNet.Disconnect();
        if (!m_loginNet.Connect(factory_client::ClientConfig::GetServerIp(),
                                factory_client::ClientConfig::GetServerPort(), m_hWnd)) {
            SetError(_T("서버에 연결할 수 없습니다. 네트워크를 확인하세요."));
            return;
        }

        CString reqJson = CPacketBuilder::BuildRegisterReq(user, pass, empId, role);
        m_loginNet.SendJson(reqJson);

        m_waitingResponse = true;
        GetDlgItem(IDOK)->EnableWindow(FALSE);
        SetError(_T("서버에 회원가입 요청 중..."));
    }
    else {
        // ── 로그인 처리 ──
        if (user.IsEmpty()) { SetError(_T("사용자 이름을 입력하세요.")); return; }
        if (pass.IsEmpty()) { SetError(_T("암호를 입력하세요.")); return; }

        // 서버에 연결하여 LOGIN_REQ 전송
        m_loginNet.Disconnect();
        if (!m_loginNet.Connect(factory_client::ClientConfig::GetServerIp(),
                                factory_client::ClientConfig::GetServerPort(), m_hWnd)) {
            SetError(_T("서버에 연결할 수 없습니다. 네트워크를 확인하세요."));
            return;
        }

        // 비밀번호를 임시 저장 (로그인 성공 시 세션에 넣기 위해)
        m_session.password = pass;

        CString reqJson = CPacketBuilder::BuildLoginReq(user, pass);
        m_loginNet.SendJson(reqJson);

        m_waitingResponse = true;
        GetDlgItem(IDOK)->EnableWindow(FALSE);
        SetError(_T("서버에 로그인 요청 중..."));
    }
}

// ============================================================================
// OnLoginRes — 서버로부터 LOGIN_RES(101) 수신
// ============================================================================
LRESULT CLoginDlg::OnLoginRes(WPARAM wParam, LPARAM lParam)
{
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    CStringA jsonA(pJson->c_str());
    delete pJson;

    m_waitingResponse = false;
    GetDlgItem(IDOK)->EnableWindow(TRUE);

    bool success = CPacketBuilder::ExtractBool(jsonA, "success");

    if (success) {
        // 서버 UTF-8 JSON → Unicode CString 변환 (한글 깨짐 방지)
        m_session.username   = CPacketBuilder::ExtractStringW(jsonA, "username");
        m_session.role       = CPacketBuilder::ExtractStringW(jsonA, "role");
        m_session.employeeId = CPacketBuilder::ExtractStringW(jsonA, "employee_id");

        m_loginNet.Disconnect();

        TRACE(_T("[LoginDlg] 서버 로그인 성공: %s (%s)\n"),
            (LPCTSTR)m_session.username, (LPCTSTR)m_session.role);
        EndDialog(IDOK);
    }
    else {
        m_session.password.Empty();
        CString msg = CPacketBuilder::ExtractStringW(jsonA, "message");
        if (msg.IsEmpty()) msg = _T("사용자 이름 또는 암호가 올바르지 않습니다.");
        SetError(msg);
        m_loginNet.Disconnect();
    }

    return 0;
}

// ============================================================================
// OnRegisterRes — 서버로부터 REGISTER_RES(105) 수신
// ============================================================================
LRESULT CLoginDlg::OnRegisterRes(WPARAM wParam, LPARAM lParam)
{
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    CStringA jsonA(pJson->c_str());
    delete pJson;

    m_waitingResponse = false;
    GetDlgItem(IDOK)->EnableWindow(TRUE);

    bool success = CPacketBuilder::ExtractBool(jsonA, "success");

    m_loginNet.Disconnect();

    if (success) {
        MessageBox(_T("회원가입이 완료되었습니다.\n로그인해주세요."),
                   _T("알림"), MB_OK | MB_ICONINFORMATION);
        SwitchMode(false);  // 로그인 모드로 복귀
    }
    else {
        CString msg = CPacketBuilder::ExtractStringW(jsonA, "message");  // UTF-8 → Unicode
        if (msg.IsEmpty()) msg = _T("회원가입에 실패했습니다.");
        SetError(msg);
    }

    return 0;
}

// ============================================================================
// OnNetDisconnected — 서버 연결 끊김
// ============================================================================
LRESULT CLoginDlg::OnNetDisconnected(WPARAM wParam, LPARAM lParam)
{
    if (m_waitingResponse) {
        m_waitingResponse = false;
        GetDlgItem(IDOK)->EnableWindow(TRUE);
        SetError(_T("서버 연결이 끊어졌습니다. 다시 시도해주세요."));
    }
    return 0;
}

// ============================================================================
// 버튼 핸들러
// ============================================================================

void CLoginDlg::OnBtnCancel()
{
    m_loginNet.Disconnect();
    EndDialog(IDCANCEL);
}

void CLoginDlg::OnBtnSwitch()
{
    SwitchMode(!m_regMode);
}

// ============================================================================
// OnPaint — 타이틀바 그래디언트 그리기
// ============================================================================
void CLoginDlg::OnPaint()
{
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);

    CRect title(0, 0, rc.right, 24);
    for (int x = 0; x < title.Width(); ++x) {
        float t = (float)x / title.Width();
        CPen pen(PS_SOLID, 1, RGB(
            (BYTE)(10 + t * 48),
            (BYTE)(36 + t * 74),
            (BYTE)(106 + t * 59)));
        CPen* p = dc.SelectObject(&pen);
        dc.MoveTo(x, 0);
        dc.LineTo(x, 24);
        dc.SelectObject(p);
    }

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(255, 255, 255));
    CFont f;
    f.CreatePointFont(90, _T("Tahoma"));
    LOGFONT lf;
    f.GetLogFont(&lf);
    lf.lfWeight = FW_BOLD;
    CFont bf;
    bf.CreateFontIndirect(&lf);
    CFont* pf = dc.SelectObject(&bf);
    CRect tr(8, 0, rc.right, 24);
    dc.DrawText(
        m_regMode ? _T("Factory QC — 회원가입") : _T("Factory QC — 사용자 인증"),
        &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    dc.SelectObject(pf);
}
