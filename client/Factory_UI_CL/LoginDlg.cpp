// ============================================================================
// LoginDlg.cpp — 로그인/회원가입 다이얼로그 구현부
// ============================================================================
// 목적:
//   사용자 인증을 처리합니다.
//   - 로그인 모드: 사용자 이름 + 비밀번호로 인증
//   - 회원가입 모드: 사원ID, 사용자이름, 비밀번호, 권한 등급 입력
//
// 현재 구현:
//   - 로컬 인증 (서버 미연결 시 하드코딩된 계정 사용)
//   - 향후 서버 연결 시 LOGIN_REQ(100) → LOGIN_RES(101) 흐름으로 확장 가능
// ============================================================================

#include "pch.h"
#include "LoginDlg.h"
#include "PacketBuilder.h"   // 로그인 요청 패킷 빌더

// ── RTTI + 메시지 맵 ─────────────────────────────────────────────────────
IMPLEMENT_DYNAMIC(CLoginDlg, CDialogEx)

BEGIN_MESSAGE_MAP(CLoginDlg, CDialogEx)
    ON_BN_CLICKED(IDOK,                OnBtnOK)       // 확인/가입 버튼
    ON_BN_CLICKED(IDCANCEL,            OnBtnCancel)   // 취소 버튼
    ON_BN_CLICKED(IDC_BTN_SWITCH_MODE, OnBtnSwitch)   // 모드 전환 링크
    ON_WM_PAINT()
END_MESSAGE_MAP()

// ============================================================================
// 생성자
// ============================================================================
CLoginDlg::CLoginDlg(CWnd* p)
    : CDialogEx(IDD_LOGIN_DLG, p)
    , m_regMode(false)    // 초기 모드: 로그인
{
}

// ============================================================================
// DoDataExchange — 컨트롤 ↔ 변수 연결 (DDX)
// ============================================================================
// DDX_Control: 다이얼로그 리소스의 컨트롤 ID를 C++ 멤버 변수에 바인딩
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

    // 기본 사용자명 미리 채우기
    m_edUser.SetWindowText(_T("admin01"));

    // 권한 등급 콤보박스 항목 추가
    m_cbRole.AddString(_T("Viewer (조회 전용)"));
    m_cbRole.AddString(_T("Operator (검사 운영)"));
    m_cbRole.AddString(_T("Admin (전체 관리)"));
    m_cbRole.SetCurSel(2);  // 기본 선택: Admin

    // 로그인 모드로 시작
    SwitchMode(false);

    return TRUE;
}

// ============================================================================
// SwitchMode — 로그인 ↔ 회원가입 모드 전환
// ============================================================================
// reg=true  → 회원가입 모드: 사원ID, 비밀번호확인, 권한 콤보박스 표시
// reg=false → 로그인 모드: 위 컨트롤들 숨김, 취소 버튼 표시
void CLoginDlg::SwitchMode(bool reg)
{
    m_regMode = reg;
    m_stErr.SetWindowText(_T(""));  // 에러 메시지 초기화

    int showReg  = reg ? SW_SHOW : SW_HIDE;  // 회원가입 전용 컨트롤
    int showLogin = reg ? SW_HIDE : SW_SHOW;  // 로그인 전용 컨트롤

    // 회원가입 전용 컨트롤 + 라벨 표시/숨김
    // 1008=사원ID 라벨, 1009=암호확인 라벨, 1010=권한등급 라벨 (.rc에서 지정)
    if (GetDlgItem(IDC_EDIT_EMPID))        GetDlgItem(IDC_EDIT_EMPID)->ShowWindow(showReg);
    if (GetDlgItem(IDC_EDIT_PASS_CONFIRM)) GetDlgItem(IDC_EDIT_PASS_CONFIRM)->ShowWindow(showReg);
    if (GetDlgItem(IDC_COMBO_ROLE))        GetDlgItem(IDC_COMBO_ROLE)->ShowWindow(showReg);
    if (GetDlgItem(1008))                  GetDlgItem(1008)->ShowWindow(showReg);  // "사원 ID:" 라벨
    if (GetDlgItem(1009))                  GetDlgItem(1009)->ShowWindow(showReg);  // "암호 확인:" 라벨
    if (GetDlgItem(1010))                  GetDlgItem(1010)->ShowWindow(showReg);  // "권한 등급:" 라벨

    // 취소 버튼은 로그인 모드에서만 표시
    if (GetDlgItem(IDCANCEL)) GetDlgItem(IDCANCEL)->ShowWindow(showLogin);

    // 모드 전환 링크 텍스트 변경
    GetDlgItem(IDC_BTN_SWITCH_MODE)->SetWindowText(
        reg ? _T("← 로그인") : _T("회원가입 →"));
    // 확인 버튼 텍스트 변경
    GetDlgItem(IDOK)->SetWindowText(reg ? _T("가입") : _T("확인"));
    // 타이틀 변경
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
    CString user, pass;
    m_edUser.GetWindowText(user);
    m_edPass.GetWindowText(pass);

    if (m_regMode) {
        // ── 회원가입 처리 ──
        CString empId, passConfirm;
        m_edEmpId.GetWindowText(empId);
        m_edPassConfirm.GetWindowText(passConfirm);

        // 입력 검증
        if (empId.IsEmpty())    { SetError(_T("사원 ID를 입력하세요.")); return; }
        if (user.IsEmpty())     { SetError(_T("사용자 이름을 입력하세요.")); return; }
        if (pass.GetLength()<4) { SetError(_T("암호는 4자 이상이어야 합니다.")); return; }
        if (pass != passConfirm){ SetError(_T("암호가 일치하지 않습니다.")); return; }

        // TODO: 서버에 회원가입 요청 전송 (서버 측 미구현)
        // 현재는 로컬에서 성공 처리
        MessageBox(_T("회원가입이 완료되었습니다."),
                   _T("알림"), MB_OK | MB_ICONINFORMATION);
        SwitchMode(false);  // 로그인 모드로 복귀
    }
    else {
        // ── 로그인 처리 ──
        if (user.IsEmpty()) { SetError(_T("사용자 이름을 입력하세요.")); return; }
        if (pass.IsEmpty()) { SetError(_T("암호를 입력하세요.")); return; }

        // 서버 로그인 시도 (비동기)
        // 현재 서버 측 LOGIN_REQ 핸들러가 미구현이므로 로컬 인증을 사용합니다.
        // 향후 서버 구현 시 아래 주석을 해제하면 됩니다:
        //
        // CNetworkClient loginNet;
        // if (loginNet.Connect(_T("127.0.0.1"), factory_client::GUI_PORT, m_hWnd)) {
        //     CString req = CPacketBuilder::BuildLoginReq(user, pass);
        //     loginNet.SendJson(req);
        //     // WM_NET_LOGIN_RES 메시지를 기다려서 처리
        //     return;
        // }

        // ── 로컬 인증 (서버 미연결 시 대체) ──
        // 하드코딩된 계정 목록으로 검증합니다.
        // 실무에서는 서버 DB에서 bcrypt 해시로 비교하지만,
        // 서버 미연결 상태의 오프라인 테스트용입니다.
        struct LocalAccount {
            LPCTSTR user;       // 사용자 이름
            LPCTSTR pass;       // 비밀번호
            LPCTSTR role;       // 권한 등급
            LPCTSTR empId;      // 사원 ID
        };

        // 로컬 계정 목록 — 필요에 따라 추가/수정 가능
        static const LocalAccount accounts[] = {
            { _T("admin01"), _T("1234"),   _T("Admin"),    _T("EMP-001") },
            { _T("oper01"),  _T("1234"),   _T("Operator"), _T("EMP-002") },
            { _T("viewer"),  _T("1234"),   _T("Viewer"),   _T("EMP-003") },
        };

        // 입력한 사용자이름 + 비밀번호가 목록에 있는지 확인
        bool found = false;
        for (const auto& acc : accounts) {
            if (user == acc.user && pass == acc.pass) {
                m_session.username   = acc.user;
                m_session.role       = acc.role;
                m_session.employeeId = acc.empId;
                found = true;
                break;
            }
        }

        if (!found) {
            SetError(_T("사용자 이름 또는 암호가 올바르지 않습니다."));
            return;
        }

        TRACE(_T("[LoginDlg] 로컬 로그인 성공: %s (%s)\n"),
            (LPCTSTR)m_session.username, (LPCTSTR)m_session.role);
        EndDialog(IDOK);  // 다이얼로그 닫기 (성공)
    }
}

// ============================================================================
// 버튼 핸들러
// ============================================================================

void CLoginDlg::OnBtnCancel()
{
    EndDialog(IDCANCEL);  // 프로그램 종료
}

void CLoginDlg::OnBtnSwitch()
{
    SwitchMode(!m_regMode);  // 현재 모드의 반대로 전환
}

// ============================================================================
// OnPaint — 타이틀바 그래디언트 그리기
// ============================================================================
void CLoginDlg::OnPaint()
{
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);

    // 상단 24px을 파란 그래디언트로 채움 (MFC 스타일 타이틀바)
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

    // 타이틀 텍스트
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
