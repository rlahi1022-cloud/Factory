// ============================================================================
// MainTabDlg.cpp — 메인 탭 다이얼로그 (앱 중심 윈도우)
// ============================================================================
// 책임:
//   로그인 성공 후 표시되는 중앙 컨트롤러. 다음을 담당:
//     1) 5개 페이지(Home/Station1/Station2/Stats/Model) 탭 관리
//     2) CNetworkClient 소유 — 서버 GUI 포트(9010) 단일 연결 유지
//     3) 서버 수신 메시지 디스패치 — 각 WM_NET_* 핸들러가 해당 페이지로 라우팅
//     4) 주기 타이머(IDT_STATUSBAR 1초) 로 연결 상태 시각화 갱신
//     5) 로그아웃 → 앱 재시작 흐름 조정
//
// 데이터 흐름 (실시간 NG):
//   서버 INSPECT_NG_PUSH(110) →
//     CNetworkClient::RecvLoop (별도 스레드) → JSON 파싱 → PostMessage
//     → OnNetNgPush(UI 스레드) → InspectionRecord 적재 → PushUpdate() →
//     Station1/Station2 페이지: SetImages + AddNgEntry, Home: 카운트 증가
//
// 데이터 흐름 (재학습 진행률):
//   서버 RETRAIN_PROGRESS_PUSH(154) →
//     OnNetRetrainProgress → PageModel::OnRetrainProgress(progress, station, type)
//     v0.11.0 부터 station/type 정보도 함께 전달 → UI 라벨에 명시
//
// 초기 로드 흐름 (접속 직후):
//   로그인 성공 → OnLoginSuccess → RequestInitialData():
//     - STATS_REQ                 (Home 대시보드)
//     - MODEL_LIST_REQ            (Model 페이지)
//     - INSPECT_HISTORY_REQ (×2)  (Station1/2 최근 NG 10건)
//     이후 각 row 더블클릭 시 INSPECT_IMAGE_REQ(116) 로 이미지 3장 on-demand 로드.
// ============================================================================

#include "pch.h"
#include "MainTabDlg.h"
#include "LoginDlg.h"       // OnLogout()에서 로그인 다이얼로그 재표시용
#include "ClientConfig.h"   // 서버 IP/포트 — config.json 기반

// ── RTTI 매크로 ──────────────────────────────────────────────────────────
// IMPLEMENT_DYNAMIC: MFC 런타임 클래스 정보 등록 (DECLARE_DYNAMIC과 쌍)
IMPLEMENT_DYNAMIC(CMainTabDlg, CDialogEx)

// ── 메시지 맵 ────────────────────────────────────────────────────────────
// BEGIN/END_MESSAGE_MAP: Windows 메시지 → 멤버 함수 매핑 테이블
// ON_WM_xxx: 표준 Windows 메시지 핸들러
// ON_COMMAND: 메뉴/버튼 커맨드 핸들러
// ON_MESSAGE: 사용자 정의 메시지 핸들러 (네트워크 수신 메시지)
BEGIN_MESSAGE_MAP(CMainTabDlg, CDialogEx)
    // ── 표준 Windows 메시지 ──
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_TIMER()

    // ── 탭 전환 알림 ──
    ON_NOTIFY(TCN_SELCHANGE, IDC_MAIN_TAB, OnTabChanged)

    // ── 메뉴 커맨드 ──
    ON_COMMAND(ID_FILE_EXIT,      OnFileExit)
    ON_COMMAND(ID_FILE_LOGOUT,    OnLogout)
    ON_COMMAND(ID_VIEW_REFRESH,   OnViewRefresh)
    ON_COMMAND(ID_INSPECT_START,  OnInspectStart)
    ON_COMMAND(ID_INSPECT_STOP,   OnInspectStop)
    ON_COMMAND(ID_HELP_ABOUT,     OnHelpAbout)
    ON_COMMAND(ID_NET_CONNECT,    OnNetConnect)
    ON_COMMAND(ID_NET_DISCONNECT, OnNetDisconnect)

    // ── 네트워크 수신 메시지 (CNetworkClient → PostMessage) ──
    ON_MESSAGE(WM_NET_CONNECTED,        OnNetConnected)
    ON_MESSAGE(WM_NET_DISCONNECTED,     OnNetDisconnectedMsg)
    ON_MESSAGE(WM_NET_NG_PUSH,          OnNetNgPush)
    ON_MESSAGE(WM_NET_OK_COUNT_PUSH,    OnNetOkCountPush)
    ON_MESSAGE(WM_NET_HEALTH_PUSH,      OnNetHealthPush)
    ON_MESSAGE(WM_NET_RESPONSE,         OnNetResponse)
    ON_MESSAGE(WM_NET_RETRAIN_PROGRESS, OnNetRetrainProgress)
    ON_MESSAGE(WM_NET_LOGIN_RES,        OnNetLoginRes)   // 로그인 응답 (프로토콜 101)
    ON_MESSAGE(WM_NET_NG_IMAGE,         OnNetNgImage)    // NG 이미지 3장 (프로토콜 110 바이너리)
END_MESSAGE_MAP()

// ============================================================================
// 생성자 / 소멸자
// ============================================================================

CMainTabDlg::CMainTabDlg(const UserSession& s, CWnd* p)
    : CDialogEx(IDD_MAIN_DLG, p)
    , m_session(s)          // 로그인 정보 저장
    , m_activeTab(0)        // 초기 탭: 종합 현황
    , m_nextId(10020)       // 시뮬레이션 검사 ID 시작값
    , m_tick(0)             // 타이머 틱 초기화
    , m_sv0(true)           // 학습 PC: 정상
    , m_sv1(true)           // 추론 PC #1: 정상
    , m_sv2(true)           // 추론 PC #2: 정상
    , m_bConnected(false)   // 네트워크: 미연결
{
    InitializeCriticalSection(&m_csRecs);
    // 초기 시뮬레이션 데이터 20건 생성
    m_recs = QCUtil::GenInitialHistory();
}

CMainTabDlg::~CMainTabDlg()
{
    // 타이머 해제
    KillTimer(IDT_LIVE_UPDATE);
    KillTimer(IDT_STATUSBAR);
    KillTimer(IDT_RECONNECT);
    KillTimer(IDT_HEARTBEAT);

    // 네트워크 연결 해제
    m_net.Disconnect();

    DeleteCriticalSection(&m_csRecs);
}

// ============================================================================
// DoDataExchange — 컨트롤과 멤버 변수 연결 (DDX)
// ============================================================================
void CMainTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    // DDX_Control: 다이얼로그 리소스의 IDC_MAIN_TAB을 m_tab 변수에 연결
    DDX_Control(pDX, IDC_MAIN_TAB, m_tab);
}

// ============================================================================
// OnInitDialog — 다이얼로그 초기화 (생성 직후 1회 호출)
// ============================================================================
BOOL CMainTabDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // ── 메뉴 로드 ──
    CMenu menu;
    menu.LoadMenu(IDR_MAINMENU);
    SetMenu(&menu);
    menu.Detach();  // CMenu 객체가 소멸되어도 메뉴 리소스는 유지

    // ── 폰트 생성 ──
    m_fNormal.CreatePointFont(90, _T("Tahoma"));        // 9pt 일반
    LOGFONT lf;
    m_fNormal.GetLogFont(&lf);
    lf.lfWeight = FW_BOLD;                              // 굵은 글꼴 파생
    m_fBold.CreateFontIndirect(&lf);
    m_fSmall.CreatePointFont(75, _T("Tahoma"));          // 7.5pt 작은 글꼴

    // ── 탭 항목 추가 ──
    m_tab.InsertItem(0, _T("종합 현황"));
    m_tab.InsertItem(1, _T("① 입고 검사"));
    m_tab.InsertItem(2, _T("② 조립 검사"));
    m_tab.InsertItem(3, _T("통계/이력"));
    m_tab.InsertItem(4, _T("모델 관리"));
    m_tab.SetFont(&m_fSmall);

    // ── 타이틀 설정 ──
    CString title;
    title.Format(_T("Factory — Smart Bottle Line Vision QC v0.12  [%s]  %s"),
        (LPCTSTR)m_session.role, (LPCTSTR)m_session.employeeId);
    SetWindowText(title);

    // ── 네트워크 자동 접속 시도 (페이지 생성 전에 선행) ──
    // 페이지 생성 중 예외가 발생해도 서버 로그인 요청은 우선 발송되도록
    // ConnectToServer를 먼저 호출한다. PostMessage는 m_hWnd에 큐잉되므로
    // 이후 메시지 핸들러가 등록될 때까지 안전하게 대기한다.
    ConnectToServer();

    // ── 페이지 생성 및 초기 표시 ──
    // 각 페이지 Create는 독립적으로 try-catch로 보호 — 한 페이지가 실패해도
    // 나머지 페이지와 네트워크 기능은 살아남도록.
    try {
        CreatePages();
    } catch (const std::exception& e) {
        TRACE(_T("[MainTabDlg] CreatePages 예외: %hs\n"), e.what());
    } catch (...) {
        TRACE(_T("[MainTabDlg] CreatePages 알 수 없는 예외\n"));
    }
    SwitchTab(0);

    // ── 타이머 시작 ──
    // IDT_LIVE_UPDATE: 3초마다 시뮬레이션 데이터 생성 (서버 미연결 시 대체)
    SetTimer(IDT_LIVE_UPDATE, 3000, nullptr);
    // IDT_STATUSBAR: 1초마다 상태바 시각 갱신
    SetTimer(IDT_STATUSBAR, 1000, nullptr);
    // IDT_HEARTBEAT: 10초마다 능동 heartbeat (v0.13.1)
    // 기존엔 recv 타임아웃(5초) 시에만 heartbeat 를 보냈는데, 푸시가 많을 땐
    // recv 가 성공해서 heartbeat 가 suppressed → 서버 recv 타임아웃 유발.
    // 별도 타이머로 무조건 주기적으로 보내 세션을 유지한다.
    SetTimer(IDT_HEARTBEAT, 10000, nullptr);

    // ── 각 페이지에 NetworkClient 주입 (생성 성공한 페이지만) ──
    if (m_stats) m_stats->SetNetworkClient(&m_net);
    if (m_model) m_model->SetNetworkClient(&m_net);
    // v0.14.3: Station1/2 Start/Stop 버튼도 서버에 명령을 보내야 하므로 주입
    if (m_st1)   m_st1  ->SetNetworkClient(&m_net);
    if (m_st2)   m_st2  ->SetNetworkClient(&m_net);

    // ── 최대화 표시 ──
    ShowWindow(SW_SHOWMAXIMIZED);
    return TRUE;
}

// ============================================================================
// 페이지 관리 함수
// ============================================================================

// CreatePages: 5개 탭 페이지를 자식 다이얼로그로 생성
void CMainTabDlg::CreatePages()
{
    // 페이지별 생성을 독립적으로 수행 — 한 페이지가 실패해도 다른 페이지는 살림
    // (PageStation1의 3분할 뷰 리소스/DDX 문제로 전체 앱이 다운되던 사례 방어)
    auto create_safe = [this](const TCHAR* name, auto&& maker, int idd) -> bool {
        try {
            maker();
            return true;
        } catch (const std::exception& e) {
            TRACE(_T("[MainTabDlg] %s 페이지 생성 예외: %hs\n"), name, e.what());
        } catch (...) {
            TRACE(_T("[MainTabDlg] %s 페이지 생성 실패 (IDD=%d)\n"), name, idd);
        }
        return false;
    };

    create_safe(_T("Home"),     [&]{ m_home  = std::make_unique<CPageHome>(this);     m_home ->Create(IDD_PAGE_HOME,     this); }, IDD_PAGE_HOME);
    create_safe(_T("Station1"), [&]{ m_st1   = std::make_unique<CPageStation1>(this); m_st1  ->Create(IDD_PAGE_STATION1, this); }, IDD_PAGE_STATION1);
    create_safe(_T("Station2"), [&]{ m_st2   = std::make_unique<CPageStation2>(this); m_st2  ->Create(IDD_PAGE_STATION2, this); }, IDD_PAGE_STATION2);
    create_safe(_T("Stats"),    [&]{ m_stats = std::make_unique<CPageStats>(this);    m_stats->Create(IDD_PAGE_STATS,    this); }, IDD_PAGE_STATS);
    create_safe(_T("Model"),    [&]{ m_model = std::make_unique<CPageModel>(this);    m_model->Create(IDD_PAGE_MODEL,    this); }, IDD_PAGE_MODEL);

    // 초기 데이터 전달 (살아남은 페이지에만)
    PushUpdate();
}

// SwitchTab: 탭 전환 — 선택된 탭의 페이지만 표시하고 나머지는 숨김
void CMainTabDlg::SwitchTab(int idx)
{
    m_activeTab = idx;
    m_tab.SetCurSel(idx);

    // 람다 함수: 페이지 표시/숨김 처리
    auto show = [&](CDialogEx* d, int t) {
        if (d && d->GetSafeHwnd())
            d->ShowWindow(m_activeTab == t ? SW_SHOW : SW_HIDE);
    };

    show(m_home.get(),  0);
    show(m_st1.get(),   1);
    show(m_st2.get(),   2);
    show(m_stats.get(), 3);
    show(m_model.get(), 4);
    LayoutPages();
}

// LayoutPages: 모든 페이지를 컨텐츠 영역에 맞게 크기 조절
void CMainTabDlg::LayoutPages()
{
    CRect cr = ContentRect();
    auto mv = [&](CDialogEx* d) {
        if (d && d->GetSafeHwnd()) d->MoveWindow(cr);
    };
    mv(m_home.get()); mv(m_st1.get()); mv(m_st2.get());
    mv(m_stats.get()); mv(m_model.get());
}

// PushUpdate: 최신 검사 데이터를 모든 페이지에 전달
// 페이지 생성 실패 시 nullptr일 수 있으므로 null 체크 필수
void CMainTabDlg::PushUpdate()
{
    EnterCriticalSection(&m_csRecs);
    auto recs_copy = m_recs;  // 스냅샷 복사 후 락 해제
    LeaveCriticalSection(&m_csRecs);

    if (m_home)  m_home ->Update(recs_copy);
    if (m_st1)   m_st1  ->Update(recs_copy);
    if (m_st2)   m_st2  ->Update(recs_copy);
    if (m_stats) m_stats->Update(recs_copy);
}

// ============================================================================
// 레이아웃 영역 계산
// ============================================================================
CRect CMainTabDlg::TitleRect()   { CRect r; GetClientRect(&r); return CRect(0, 0, r.right, 22); }
CRect CMainTabDlg::ToolbarRect() { CRect r; GetClientRect(&r); return CRect(0, 22, r.right, 46); }
CRect CMainTabDlg::ContentRect()
{
    CRect r; GetClientRect(&r);
    CRect tabRc(0, 46, r.right, 72);
    if (m_tab.GetSafeHwnd()) m_tab.MoveWindow(tabRc);
    return CRect(4, 72, r.right - 4, r.bottom - 18);
}
CRect CMainTabDlg::StatusRect() { CRect r; GetClientRect(&r); return CRect(0, r.bottom - 18, r.right, r.bottom); }

// ============================================================================
// 커스텀 그리기
// ============================================================================

BOOL CMainTabDlg::OnEraseBkgnd(CDC* pDC)
{
    // 배경을 MFC 스타일 회색으로 채움
    CRect rc; GetClientRect(&rc);
    pDC->FillSolidRect(&rc, RGB(212, 208, 200));
    return TRUE;
}

void CMainTabDlg::OnPaint()
{
    CPaintDC dc(this);
    DrawTitle(dc);
    DrawToolbar(dc);
    DrawStatus(dc);
}

// DrawTitle: 상단 그래디언트 타이틀바 (파란색 계열)
void CMainTabDlg::DrawTitle(CDC& dc)
{
    CRect rc = TitleRect();
    // 왼쪽(진한 파랑) → 오른쪽(밝은 파랑) 수평 그래디언트
    for (int x = 0; x < rc.Width(); ++x) {
        float t = (float)x / rc.Width();  // 0.0 ~ 1.0 보간 비율
        CPen pen(PS_SOLID, 1, RGB(
            (BYTE)(10 + t * 48),    // R: 10 → 58
            (BYTE)(36 + t * 74),    // G: 36 → 110
            (BYTE)(106 + t * 59))); // B: 106 → 165
        CPen* p = dc.SelectObject(&pen);
        dc.MoveTo(x, 0); dc.LineTo(x, 22);
        dc.SelectObject(p);
    }
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(255, 255, 255));
    CFont* pf = dc.SelectObject(&m_fBold);
    CString title;
    title.Format(_T("Factory — Smart Bottle Line Vision QC v0.12  [%s]  %s"),
        (LPCTSTR)m_session.role, (LPCTSTR)m_session.employeeId);
    CRect tr(6, 0, rc.right - 60, 22);
    dc.DrawText(title, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    dc.SelectObject(pf);
}

// DrawToolbar: 툴바 영역 + 서버 상태 LED
void CMainTabDlg::DrawToolbar(CDC& dc)
{
    CRect rc = ToolbarRect();
    dc.FillSolidRect(&rc, RGB(236, 233, 216));

    // 하단 경계선
    CPen pen(PS_SOLID, 1, RGB(128, 128, 128));
    CPen* p = dc.SelectObject(&pen);
    dc.MoveTo(rc.left, rc.bottom - 1);
    dc.LineTo(rc.right, rc.bottom - 1);
    dc.SelectObject(p);

    // ── 네트워크 상태 표시 (왼쪽) ──
    dc.SetBkMode(TRANSPARENT);
    CFont* pf = dc.SelectObject(&m_fSmall);
    CString netStatus;
    netStatus.Format(_T("서버: %s"), m_bConnected ? _T("연결됨") : _T("미연결"));
    dc.SetTextColor(m_bConnected ? RGB(0, 128, 0) : RGB(200, 0, 0));
    CRect nsr(8, rc.top + 4, 120, rc.bottom - 2);
    dc.DrawText(netStatus, &nsr, DT_LEFT | DT_SINGLELINE);
    dc.SelectObject(pf);

    // ── 서버 LED (오른쪽) ──
    int x = rc.right - 340;
    struct { bool ok; LPCTSTR lbl; } svs[] = {
        {m_sv0, _T("학습 PC")},
        {m_sv1, _T("추론 PC #1")},
        {m_sv2, _T("추론 PC #2")}
    };
    for (auto& s : svs) {
        DrawLed(dc, x, rc.top + 7, s.ok, s.lbl);
        x += 115;
    }
}

// DrawLed: 원형 LED 인디케이터 + 라벨 텍스트
void CMainTabDlg::DrawLed(CDC& dc, int x, int y, bool ok, LPCTSTR lbl)
{
    // LED 색상: 정상=초록, 장애=빨강
    COLORREF col = ok ? RGB(0, 176, 80) : RGB(255, 0, 0);
    CBrush br(col);
    CPen pen(PS_SOLID, 1, RGB(80, 80, 80));
    CPen* pp = dc.SelectObject(&pen);
    CBrush* pb = dc.SelectObject(&br);
    dc.Ellipse(x, y, x + 10, y + 10);   // 10x10 원 그리기
    dc.SelectObject(pp);
    dc.SelectObject(pb);

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(0, 0, 0));
    CFont* pf = dc.SelectObject(&m_fSmall);
    CRect tr(x + 13, y - 2, x + 110, y + 12);
    dc.DrawText(lbl, &tr, DT_LEFT | DT_SINGLELINE);
    dc.SelectObject(pf);
}

// DrawStatus: 하단 상태바 (TCP 상태, DB 상태, 마지막 검사 시각)
void CMainTabDlg::DrawStatus(CDC& dc)
{
    CRect rc = StatusRect();
    dc.FillSolidRect(&rc, RGB(236, 233, 216));

    // 상단 경계선
    CPen pen(PS_SOLID, 1, RGB(128, 128, 128));
    CPen* p = dc.SelectObject(&pen);
    dc.MoveTo(rc.left, rc.top);
    dc.LineTo(rc.right, rc.top);
    dc.SelectObject(p);

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(128, 128, 128));
    CFont* pf = dc.SelectObject(&m_fSmall);

    // 왼쪽: TCP/DB 상태 + 마지막 검사 시각
    EnterCriticalSection(&m_csRecs);
    InspectionRecord last = m_recs.empty() ? InspectionRecord{} : m_recs.back();
    LeaveCriticalSection(&m_csRecs);
    CString leftText;
    leftText.Format(_T("TCP: :%d %s | DB: MariaDB | 마지막 검사: %s"),
        factory_client::ClientConfig::GetServerPort(),
        m_bConnected ? _T("연결") : _T("미연결"),
        (LPCTSTR)last.time);
    CRect lr(6, rc.top, rc.right / 2, rc.bottom);
    dc.DrawText(leftText, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 오른쪽: 검사 번호 + 현재 시각
    SYSTEMTIME st;
    GetLocalTime(&st);
    CString rightText;
    rightText.Format(_T("검사 #%d | %d-%02d-%02d %02d:%02d:%02d"),
        last.id, st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    CRect rr(rc.right / 2, rc.top, rc.right - 6, rc.bottom);
    dc.DrawText(rightText, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    dc.SelectObject(pf);
}

// ============================================================================
// Windows 이벤트 핸들러
// ============================================================================

void CMainTabDlg::OnSize(UINT t, int cx, int cy)
{
    CDialogEx::OnSize(t, cx, cy);
    if (m_tab.GetSafeHwnd()) { LayoutPages(); Invalidate(); }
}

void CMainTabDlg::OnTimer(UINT_PTR id)
{
    if (id == IDT_LIVE_UPDATE) {
        // ── 시뮬레이션 데이터 생성 ──
        // 서버 미연결 시에도 UI가 동작하도록 시뮬레이션 데이터를 계속 생성합니다.
        // 서버 연결 시에는 서버 푸시 데이터와 시뮬레이션 데이터가 병행됩니다.
        ++m_tick;
        // 추론 PC #2를 20틱마다 1틱동안 장애 시뮬레이션 (서버 미연결 시)
        if (!m_bConnected) {
            m_sv2 = (m_tick % 20 != 15);
        }
        {
            EnterCriticalSection(&m_csRecs);
            m_recs.push_back(QCUtil::GenRecord(m_nextId++));
            if (m_recs.size() > 50) m_recs.erase(m_recs.begin());
            LeaveCriticalSection(&m_csRecs);
        }
        PushUpdate();
        if (m_st1) m_st1->Tick();
        if (m_st2) m_st2->Tick();
        InvalidateRect(ToolbarRect());
        InvalidateRect(StatusRect());
    }
    else if (id == IDT_STATUSBAR) {
        // ── 연결 상태 실시간 동기화 ──
        // UI의 m_bConnected는 WM_NET_CONNECTED/DISCONNECTED 메시지에 의존하는데,
        // 네트워크 소리 없이 끊어진 경우(silent drop) 이 메시지가 안 올 수 있다.
        // → 1초마다 실제 소켓 상태를 확인해 m_bConnected와 동기화한다.
        bool actual = m_net.IsConnected();
        if (actual != m_bConnected) {
            m_bConnected = actual;
            TRACE(_T("[MainTabDlg] 연결 상태 동기화: %s\n"),
                  actual ? _T("연결됨") : _T("끊김 감지"));
            if (!actual) {
                // 끊김 감지 → 재접속 타이머 시작 (중복 방지 위해 KillTimer 선호출)
                KillTimer(IDT_RECONNECT);
                SetTimer(IDT_RECONNECT, 10000, nullptr);
            }
            InvalidateRect(ToolbarRect());  // LED/상태 그림 갱신
        }
        InvalidateRect(StatusRect());
    }
    else if (id == IDT_RECONNECT) {
        // ── 서버 재접속 시도 ──
        if (!m_net.IsConnected()) {
            TRACE(_T("[MainTabDlg] 서버 재접속 시도...\n"));
            ConnectToServer();  // 접속 + LOGIN_REQ 전송
            if (m_net.IsConnected()) {
                KillTimer(IDT_RECONNECT);
            }
        } else {
            KillTimer(IDT_RECONNECT);
        }
    }
    else if (id == IDT_HEARTBEAT) {
        // v0.13.1: recv 상태와 무관하게 10초마다 능동 heartbeat 송신.
        // 이유: 서버 푸시가 빈번하면 recv 타임아웃이 안 떠서 기존 heartbeat(recv-타임아웃
        //      기반) 가 suppressed 됨 → 서버가 클라→서버 트래픽이 없다고 판단해
        //      recv 타임아웃으로 close 시키던 문제.
        // 연결 상태일 때만 보내고, 실패해도 조용히 넘김 (다음 주기에 재시도).
        if (m_net.IsConnected()) {
            CString pkt = CPacketBuilder::BuildAck(factory_client::EXT_ACK, _T("heartbeat"));
            m_net.SendJson(pkt);
        }
    }
    CDialogEx::OnTimer(id);
}

void CMainTabDlg::OnTabChanged(NMHDR*, LRESULT*)
{
    int sel = m_tab.GetCurSel();
    if (sel >= 0) SwitchTab(sel);
}

void CMainTabDlg::OnCancel()
{
    m_net.Disconnect();   // 네트워크 정리
    DestroyWindow();
    PostQuitMessage(0);
}

// ============================================================================
// 메뉴 커맨드 핸들러
// ============================================================================

void CMainTabDlg::OnFileExit()     { OnCancel(); }
void CMainTabDlg::OnViewRefresh()  { PushUpdate(); }

// v0.14.0: 검사 시작/중지 — 실제 AI 추론서버에 pause/resume 명령 전송.
// station_filter=0 (전체) 로 모든 추론서버에 적용. 응답(161)은 OnNetResponse 에서 처리.
// 로컬 시뮬레이션 타이머(IDT_LIVE_UPDATE)는 서버 미연결 시 보조용으로 유지.
void CMainTabDlg::OnInspectStart()
{
    if (m_net.IsConnected()) {
        CString req = CPacketBuilder::BuildInspectControlReq(0, _T("resume"));
        m_net.SendJson(req);
        TRACE(_T("[MainTabDlg] 검사 재개 요청 송신 (action=resume)\n"));
    } else {
        // 서버 미연결 → 시뮬레이션 모드 타이머만 재가동
        SetTimer(IDT_LIVE_UPDATE, 3000, nullptr);
    }
}

void CMainTabDlg::OnInspectStop()
{
    if (m_net.IsConnected()) {
        CString req = CPacketBuilder::BuildInspectControlReq(0, _T("pause"));
        m_net.SendJson(req);
        TRACE(_T("[MainTabDlg] 검사 일시정지 요청 송신 (action=pause)\n"));
    } else {
        KillTimer(IDT_LIVE_UPDATE);
    }
}

void CMainTabDlg::OnHelpAbout()
{
    MessageBox(
        _T("Factory — Smart Bottle Line Vision QC v0.12\n\n")
        _T("PatchCore (입고 이상탐지)\n")
        _T("YOLO11   (조립 결함 검출)\n\n")
        _T("Pylon SDK | OpenCV | Arduino Serial\n\n")
        _T("네트워크: TCP/IP (포트 9010)"),
        _T("정보"), MB_OK | MB_ICONINFORMATION);
}

// ============================================================================
// ConnectToServer — 서버 접속 시도 + 접속 직후 LOGIN_REQ 전송
// ============================================================================
// 서버(gui_tcp_listener)는 클라이언트 접속 후 첫 패킷을 recv로 대기합니다.
// 클라이언트가 아무것도 안 보내면 서버가 세션을 끊어버릴 수 있으므로,
// 접속 성공 직후 LOGIN_REQ 패킷을 즉시 전송하여 세션을 유지합니다.
void CMainTabDlg::ConnectToServer()
{
    if (m_net.IsConnected()) return;

    if (m_net.Connect(factory_client::ClientConfig::GetServerIp(),
                      factory_client::ClientConfig::GetServerPort(), m_hWnd)) {
        // ── 접속 직후 LOGIN_REQ 전송 (서버 세션 유지 핵심!) ──
        // 서버의 handle_client()는 recv_one_request()로 첫 패킷을 기다리고 있음.
        // 이걸 안 보내면 서버가 "클라이언트가 아무 요청도 안 함" → 세션 제거.
        CString loginJson = CPacketBuilder::BuildLoginReq(
            m_session.username, m_session.password);
        m_net.SendJson(loginJson);

        // 접속 직후 초기 데이터 요청 (DB 기반 실데이터 로드)
        if (m_model) m_model->RequestModelList();

        // 검사 이력 + 통계를 자동 조회 → 홈 화면에 실데이터 표시
        // v0.13.2: 홈 NG 리스트가 20+건 나오도록 limit 200 으로 확대
        //          (NG 비율 10% 가정 → 평균 20건 확보)
        CString histReq = CPacketBuilder::BuildInspectHistoryReq(
            0, _T(""), _T(""), 200);
        m_net.SendJson(histReq);

        CString statsReq = CPacketBuilder::BuildStatsReq(0, _T(""), _T(""));
        m_net.SendJson(statsReq);

        TRACE(_T("[MainTabDlg] 서버 접속 + LOGIN_REQ + 초기 데이터 요청 완료\n"));
    } else {
        TRACE(_T("[MainTabDlg] 서버 자동 접속 실패 — 시뮬레이션 모드\n"));
        SetTimer(IDT_RECONNECT, 10000, nullptr);
    }
}

// OnNetConnect: 메뉴에서 수동으로 서버 접속
void CMainTabDlg::OnNetConnect()
{
    if (m_net.IsConnected()) {
        MessageBox(_T("이미 서버에 연결되어 있습니다."), _T("알림"), MB_OK);
        return;
    }
    ConnectToServer();
    if (!m_net.IsConnected()) {
        CString errMsg;
        errMsg.Format(_T("서버 접속 실패\n\n서버 IP: %s\n포트: %d"),
            (LPCTSTR)factory_client::ClientConfig::GetServerIp(),
            factory_client::ClientConfig::GetServerPort());
        MessageBox(errMsg, _T("접속 실패"), MB_OK | MB_ICONERROR);
    }
}

// OnNetDisconnect: 메뉴에서 수동으로 서버 해제
void CMainTabDlg::OnNetDisconnect()
{
    m_net.Disconnect();
}

// ============================================================================
// OnLogout — 로그아웃 → 로그인 화면으로 복귀
// ============================================================================
// 동작:
//   1) 서버에 LOGOUT_REQ 전송
//   2) 네트워크 연결 해제
//   3) 현재 메인 윈도우 숨김
//   4) 로그인 다이얼로그 재표시
//   5) 로그인 성공 → 세션 갱신 + 다시 표시
//   6) 로그인 취소 → 프로그램 종료
void CMainTabDlg::OnLogout()
{
    // 서버에 로그아웃 알림
    if (m_net.IsConnected()) {
        CString logoutJson = CPacketBuilder::BuildLogoutReq(m_session.username);
        m_net.SendJson(logoutJson);
        m_net.Disconnect();
    }

    // 타이머 일시 중지
    KillTimer(IDT_LIVE_UPDATE);
    KillTimer(IDT_RECONNECT);

    // 메인 윈도우 숨김
    ShowWindow(SW_HIDE);

    // 로그인 다이얼로그 재표시
    CLoginDlg loginDlg(this);
    INT_PTR nRes = loginDlg.DoModal();
    if (nRes != IDOK) {
        // 취소 → 프로그램 종료
        OnCancel();
        return;
    }

    // 새 세션 정보로 갱신
    m_session = loginDlg.m_session;

    // 타이틀 업데이트
    CString title;
    title.Format(_T("Factory — Smart Bottle Line Vision QC v0.12  [%s]  %s"),
        (LPCTSTR)m_session.role, (LPCTSTR)m_session.employeeId);
    SetWindowText(title);

    // 다시 표시 + 타이머 재시작
    ShowWindow(SW_SHOWMAXIMIZED);
    SetTimer(IDT_LIVE_UPDATE, 3000, nullptr);
    InvalidateRect(TitleRect());
    InvalidateRect(ToolbarRect());

    // 서버 재접속
    ConnectToServer();
}

// ============================================================================
// 네트워크 메시지 핸들러 (서버로부터 수신한 데이터 처리)
// ============================================================================
// 중요: LPARAM에 담긴 std::string*는 이 핸들러에서 반드시 delete 해야 합니다!
//       (CNetworkClient가 new로 할당하여 PostMessage로 전달함)

// OnNetConnected: 서버 접속 성공 (WM_NET_CONNECTED)
LRESULT CMainTabDlg::OnNetConnected(WPARAM, LPARAM)
{
    m_bConnected = true;
    TRACE(_T("[MainTabDlg] 서버 연결됨\n"));

    // 재접속 타이머 중지
    KillTimer(IDT_RECONNECT);

    // 툴바 갱신 (연결 상태 표시)
    InvalidateRect(ToolbarRect());
    InvalidateRect(StatusRect());

    return 0;
}

// OnNetDisconnectedMsg: 서버 연결 끊김 (WM_NET_DISCONNECTED)
LRESULT CMainTabDlg::OnNetDisconnectedMsg(WPARAM, LPARAM)
{
    m_bConnected = false;
    // 재접속 시 스테이션 초기 이미지 다시 로드되도록 플래그 리셋
    m_initialImagesLoaded = false;
    TRACE(_T("[MainTabDlg] 서버 연결 끊김\n"));

    // 재접속 타이머 시작 (10초 후 재시도)
    SetTimer(IDT_RECONNECT, 10000, nullptr);

    InvalidateRect(ToolbarRect());
    InvalidateRect(StatusRect());

    return 0;
}

// OnNetNgPush: NG 검사 결과 수신 (WM_NET_NG_PUSH, 프로토콜 110)
// 서버가 추론 결과 중 NG인 것을 실시간으로 푸시합니다.
LRESULT CMainTabDlg::OnNetNgPush(WPARAM, LPARAM lParam)
{
    // LPARAM에서 JSON 문자열 포인터 꺼내기
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    // JSON 파싱 → InspectionRecord로 변환
    CStringA jsonA(pJson->c_str());
    InspectionRecord rec;
    rec.id        = m_nextId++;
    rec.station   = CPacketBuilder::ExtractInt(jsonA, "station_id");
    rec.isNG      = true;  // NG 푸시이므로 항상 true
    rec.score     = CPacketBuilder::ExtractDouble(jsonA, "score");
    rec.latencyMs = CPacketBuilder::ExtractInt(jsonA, "latency_ms");

    // 시각 추출 (ISO8601에서 HH:MM:SS 부분)
    CStringA tsA = CPacketBuilder::ExtractString(jsonA, "timestamp");
    if (tsA.GetLength() >= 19) {
        // "2026-04-16T14:30:00" → "14:30:00"
        rec.time = CString(tsA.Mid(11, 8));
    } else {
        SYSTEMTIME st; GetLocalTime(&st);
        rec.time.Format(_T("%02d:%02d:%02d"), st.wHour, st.wMinute, st.wSecond);
    }

    // 결함 유형 변환
    CStringA defectA = CPacketBuilder::ExtractString(jsonA, "defect_type");
    if (defectA == "anomaly")         rec.defect = EDefect::Anomaly;
    else if (defectA == "cap_loose")  rec.defect = EDefect::CapLoose;
    else if (defectA == "cap_missing") rec.defect = EDefect::CapMissing;
    else if (defectA == "label_tilt") rec.defect = EDefect::LabelTilt;
    else if (defectA == "label_torn") rec.defect = EDefect::LabelTorn;
    else if (defectA == "fill_low")   rec.defect = EDefect::FillLow;
    else                              rec.defect = EDefect::Anomaly;

    // 이력에 추가 (스레드 보호)
    EnterCriticalSection(&m_csRecs);
    m_recs.push_back(rec);
    if (m_recs.size() > 50) m_recs.erase(m_recs.begin());
    LeaveCriticalSection(&m_csRecs);

    // v0.13.2: 홈 페이지 NG 리스트 맨 위에 prepend (실시간)
    if (m_home && rec.isNG) m_home->AddNgRow(rec);

    // 모든 페이지 업데이트
    PushUpdate();
    if (m_st1) m_st1->Tick();
    if (m_st2) m_st2->Tick();
    InvalidateRect(StatusRect());

    // 힙에 할당된 JSON 문자열 해제 (메모리 누수 방지!)
    delete pJson;
    return 0;
}

// OnNetOkCountPush: OK/NG 카운트 수신 (WM_NET_OK_COUNT_PUSH, 프로토콜 112)
// 서버가 주기적으로(5초마다) 각 스테이션의 OK/NG 누적 카운트를 전송합니다.
LRESULT CMainTabDlg::OnNetOkCountPush(WPARAM, LPARAM lParam)
{
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    CStringA jsonA(pJson->c_str());
    int stationId  = CPacketBuilder::ExtractInt(jsonA, "station_id");
    int okCount    = CPacketBuilder::ExtractInt(jsonA, "ok_count");
    int ngCount    = CPacketBuilder::ExtractInt(jsonA, "ng_count");

    // 홈 페이지의 스테이션별 OK/NG 표시에 반영
    if (m_home) m_home->UpdateStationCount(stationId, okCount, ngCount);
    TRACE(_T("[MainTabDlg] OK카운트 수신: station=%d ok=%d ng=%d\n"),
        stationId, okCount, ngCount);

    delete pJson;
    return 0;
}

// OnNetHealthPush: 서버 헬스 상태 수신 (WM_NET_HEALTH_PUSH, 프로토콜 170)
// 서버가 추론서버/학습서버의 상태(정상/장애)를 감지하여 클라이언트에 알립니다.
LRESULT CMainTabDlg::OnNetHealthPush(WPARAM, LPARAM lParam)
{
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    CStringA jsonA(pJson->c_str());
    CStringA serverName = CPacketBuilder::ExtractString(jsonA, "server_name");
    CStringA status     = CPacketBuilder::ExtractString(jsonA, "status");
    bool isOk = (status != "down");  // "recovered" → 정상, "down" → 장애

    // 서버 이름으로 해당 LED 업데이트
    if (serverName.Find("train") >= 0 || serverName.Find("learning") >= 0) {
        m_sv0 = isOk;       // 학습 PC
    } else if (serverName.Find("1") >= 0 || serverName.Find("inbound") >= 0) {
        m_sv1 = isOk;       // 추론 PC #1
    } else if (serverName.Find("2") >= 0 || serverName.Find("assembly") >= 0) {
        m_sv2 = isOk;       // 추론 PC #2
    }

    TRACE(_T("[MainTabDlg] 헬스 상태: %S → %S\n"),
        (LPCSTR)serverName, (LPCSTR)status);

    // 툴바(LED 표시) 갱신
    InvalidateRect(ToolbarRect());

    delete pJson;
    return 0;
}

// OnNetResponse: 범용 응답 수신 (WM_NET_RESPONSE)
// WPARAM에 protocol_no가 담겨 있어 메시지 종류를 구분할 수 있습니다.
LRESULT CMainTabDlg::OnNetResponse(WPARAM wParam, LPARAM lParam)
{
    int protocolNo = static_cast<int>(wParam);
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    TRACE(_T("[MainTabDlg] 응답 수신: protocol_no=%d\n"), protocolNo);

    switch (protocolNo) {
    case factory_client::INSPECT_HISTORY_RES:
        // 검사 이력 응답 → 통계 페이지 + 홈 NG 리스트에 전달 (v0.13.2)
        if (m_stats) m_stats->OnInspectHistoryRes(*pJson);
        if (m_home)  m_home ->OnInspectHistoryRes(*pJson);

        // 접속 직후 1회만: 스테이션별 최신 N건(기본 10)의 이미지를 자동 요청하여
        // 상단 대형 3뷰 + 하단 이력 리스트에 DB 기반 실데이터를 즉시 표시.
        // 라이브 NG_PUSH가 오면 상단은 덮어쓰고, 하단 리스트에는 추가됨.
        if (!m_initialImagesLoaded && m_stats) {
            m_initialImagesLoaded = true;
            // v0.14.4: 초기 이미지 preload 를 대폭 축소.
            //   이전엔 Station1/2 × 10 = 20장 × 약 5MB = 100MB 를 로그인 직후
            //   쏟아부어서 TCP 가 터지고 연결이 끊어지는 현상 발생.
            //   → Station 당 최신 1장만 preload (총 2장 × 5MB = 10MB) 로 제한.
            //   나머지 이력은 사용자가 해당 row 를 클릭할 때 on-demand 로 로드.
            constexpr int kHistoryImagesPerStation = 1;
            auto ids1 = m_stats->GetRecentInspectionIdsByStation(1, kHistoryImagesPerStation);
            auto ids2 = m_stats->GetRecentInspectionIdsByStation(2, kHistoryImagesPerStation);
            // 역순으로 보냄(오래된 것 먼저) — 응답이 오는 순서대로 리스트 앞에 prepend되면
            // 최종적으로 시간 역순(최신이 맨 위)으로 정렬됨.
            for (auto it = ids1.rbegin(); it != ids1.rend(); ++it) {
                m_net.SendJson(CPacketBuilder::BuildInspectImageReq(*it));
            }
            for (auto it = ids2.rbegin(); it != ids2.rend(); ++it) {
                m_net.SendJson(CPacketBuilder::BuildInspectImageReq(*it));
            }
            TRACE(_T("[MainTabDlg] 초기 이력 이미지 요청 | s1=%d건 s2=%d건\n"),
                  (int)ids1.size(), (int)ids2.size());
        }
        break;

    case factory_client::STATS_RES:
        // 통계 데이터 응답
        if (m_stats) m_stats->OnStatsRes(*pJson);
        break;

    case factory_client::MODEL_LIST_RES:
        // 모델 목록 응답 → 모델 페이지에 전달
        if (m_model) m_model->OnModelListRes(*pJson);
        break;

    case factory_client::RETRAIN_RES:
        // 재학습 시작 응답
        if (m_model) m_model->OnRetrainRes(*pJson);
        break;

    case factory_client::RETRAIN_UPLOAD_ACK:
        // v0.13.0: 학습 이미지 업로드 개별 ACK — 진행률 업데이트 + 전부 끝나면 RETRAIN_REQ 발행
        if (m_model) m_model->OnRetrainUploadAck(*pJson);
        break;

    case factory_client::INSPECT_CONTROL_RES: {
        // v0.14.0: 검사 pause/resume 결과 — 상태바에 간단히 표시
        CStringA jsonA(pJson->c_str());
        bool ok = CPacketBuilder::ExtractBool(jsonA, "success");
        CString action = CPacketBuilder::ExtractStringW(jsonA, "action");
        int applied = CPacketBuilder::ExtractInt(jsonA, "applied_count");
        CString msg;
        if (ok) {
            msg.Format(_T("검사 %s 명령 적용됨 (%d대 추론서버)"),
                       (LPCTSTR)action, applied);
        } else {
            CString err = CPacketBuilder::ExtractStringW(jsonA, "message");
            msg.Format(_T("검사 %s 실패: %s"), (LPCTSTR)action, (LPCTSTR)err);
        }
        TRACE(_T("[MainTabDlg] %s\n"), (LPCTSTR)msg);
        // 사용자에게 팝업으로도 간단히 알림 (선택적으로 상태바만 해도 됨)
        MessageBox(msg, _T("검사 제어"), MB_OK | MB_ICONINFORMATION);
        break;
    }

    default:
        TRACE(_T("[MainTabDlg] 미처리 응답: %d\n"), protocolNo);
        break;
    }

    delete pJson;
    return 0;
}

// OnNetRetrainProgress: 재학습 진행률 수신 (WM_NET_RETRAIN_PROGRESS, 프로토콜 154)
LRESULT CMainTabDlg::OnNetRetrainProgress(WPARAM, LPARAM lParam)
{
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    CStringA jsonA(pJson->c_str());
    int     progress   = CPacketBuilder::ExtractInt(jsonA, "progress");
    int     stationId  = CPacketBuilder::ExtractInt(jsonA, "station_id");
    CString modelType  = CPacketBuilder::ExtractStringW(jsonA, "model_type");

    // 모델 페이지에 진행률 전달 — station/type 정보도 함께 넘겨 UI에 표시
    if (m_model) m_model->OnRetrainProgress(progress, stationId, modelType);
    TRACE(_T("[MainTabDlg] 재학습 진행률: station=%d type=%s %d%%\n"),
          stationId, (LPCTSTR)modelType, progress);

    delete pJson;
    return 0;
}

// ============================================================================
// OnNetLoginRes — 로그인 응답 수신 (WM_NET_LOGIN_RES, 프로토콜 101)
// ============================================================================
// 서버가 LOGIN_REQ(100)를 처리한 결과를 반환합니다.
// result=0 이면 인증 성공, 그 외는 실패(재접속 타이머 시작).
// 이 핸들러가 없으면 서버는 응답 후 다음 요청을 기다리다가
// 클라이언트가 아무것도 안 보내는 것으로 판단하여 세션을 끊어버립니다.
LRESULT CMainTabDlg::OnNetLoginRes(WPARAM, LPARAM lParam)
{
    std::string* pJson = reinterpret_cast<std::string*>(lParam);
    if (!pJson) return 0;

    CStringA jsonA(pJson->c_str());
    int result = CPacketBuilder::ExtractInt(jsonA, "result");  // 0=성공, 그 외=실패

    if (result == 0) {
        TRACE(_T("[MainTabDlg] LOGIN_RES 수신: 인증 성공 — 세션 유지\n"));
        // m_bConnected는 WM_NET_CONNECTED에서 이미 true로 설정됨
        // 필요 시 여기서 초기 데이터 요청 추가 가능:
        // m_net.SendJson(CPacketBuilder::BuildModelListReq());
    } else {
        TRACE(_T("[MainTabDlg] LOGIN_RES 수신: 인증 실패 (result=%d)\n"), result);
        m_net.Disconnect();
        // Disconnect 후 OnNetDisconnectedMsg가 호출되어 재접속 타이머 시작
    }

    delete pJson;
    return 0;
}

// ============================================================================
// OnNetNgImage — NG 이미지 3장 수신 (WM_NET_NG_IMAGE, 프로토콜 110 바이너리)
// ============================================================================
// NetworkClient가 JSON + 3장 바이너리를 분해하여 NgImagePacket으로 전달.
// station_id에 따라 PageStation1 또는 PageStation2에 이미지를 주입한다.
// 각 페이지가 원본/히트맵/마스크를 해당 뷰(CCameraView/CHeatmapView/CPredMaskView)
// 에 SetImage()로 주입 → OnPaint에서 BitBlt으로 렌더링.
LRESULT CMainTabDlg::OnNetNgImage(WPARAM, LPARAM lParam)
{
    NgImagePacket* pkt = reinterpret_cast<NgImagePacket*>(lParam);
    if (!pkt) return 0;

    TRACE(_T("[MainTabDlg] NG 이미지 수신 | station=%d id=%d img=%zu heat=%zu mask=%zu\n"),
          pkt->station_id, pkt->inspection_id,
          pkt->image.size(), pkt->heatmap.size(), pkt->pred_mask.size());

    // 메타데이터(timestamp/score)는 PageStats 히스토리 캐시에서 조회.
    // v0.14.7: 캐시 미발견(방금 발생한 새 NG) 시 패킷 자체에 담겨 온
    //   timestamp_iso / score 를 폴백으로 사용 → 리스트의 "--:--:--" 와 0.00 해소.
    CString timeLabel = _T("--:--:--");
    double  score     = 0.0;
    const bool found  = (m_stats && m_stats->LookupInspectionMeta(pkt->inspection_id, timeLabel, score));
    if (!found) {
        // ISO8601("2026-04-22T09:12:34.567+00:00") 에서 "HH:MM:SS" 구간만 추출.
        // 길이 부족하면 기본 "--:--:--" 유지.
        if (pkt->timestamp_iso.GetLength() >= 19) {
            timeLabel = pkt->timestamp_iso.Mid(11, 8);
        }
        if (pkt->score > 0.0) {
            score = pkt->score;
        }
    }

    if (pkt->station_id == 1 && m_st1) {
        // 상단 대형 3뷰 — 최신 1건만 유지 (덮어쓰기)
        m_st1->SetImages(pkt->image, pkt->heatmap, pkt->pred_mask);
        // 하단 이력 리스트 — 최대 10건까지 누적 (동일 id는 교체)
        m_st1->AddNgEntry(pkt->inspection_id, score, timeLabel,
                          pkt->image, pkt->heatmap, pkt->pred_mask);
    } else if (pkt->station_id == 2 && m_st2) {
        m_st2->SetImages(pkt->image, pkt->heatmap, pkt->pred_mask);
        // 스테이션2는 후속 커밋에서 리스트 연결 예정
    }

    delete pkt;
    return 0;
}
