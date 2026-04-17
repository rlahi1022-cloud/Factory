// ============================================================================
// PageHome.cpp — 종합 현황 페이지 구현부
// ============================================================================
// 목적:
//   전체 검사 현황을 한눈에 보여주는 메인 대시보드 페이지입니다.
//   - 종합 OK/NG 카운트 및 불량률
//   - 스테이션별(입고/조립) 개별 OK/NG 카운트
//   - 최근 NG 검사 이력 리스트
// ============================================================================

#include "pch.h"
#include "PageHome.h"

IMPLEMENT_DYNAMIC(CPageHome, CDialogEx)
BEGIN_MESSAGE_MAP(CPageHome, CDialogEx)
    ON_WM_PAINT()
END_MESSAGE_MAP()

CPageHome::CPageHome(CWnd* p) : CDialogEx(IDD_PAGE_HOME, p) {}

void CPageHome::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    // NG 이력 리스트 컨트롤 바인딩
    DDX_Control(pDX, IDC_LIST_NG, m_listNG);
}

BOOL CPageHome::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // NG 이력 리스트뷰 설정
    m_listNG.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listNG.InsertColumn(0, _T("ID"),       LVCFMT_LEFT, 50);
    m_listNG.InsertColumn(1, _T("스테이션"), LVCFMT_LEFT, 60);
    m_listNG.InsertColumn(2, _T("시각"),     LVCFMT_LEFT, 70);
    m_listNG.InsertColumn(3, _T("결과"),     LVCFMT_LEFT, 40);
    m_listNG.InsertColumn(4, _T("점수"),     LVCFMT_LEFT, 50);
    m_listNG.InsertColumn(5, _T("결함"),     LVCFMT_LEFT, 110);
    m_listNG.InsertColumn(6, _T("Latency"),  LVCFMT_LEFT, 60);

    return TRUE;
}

// ============================================================================
// Update — 검사 데이터 갱신
// ============================================================================
// 파라미터: recs — 전체 검사 이력 (최대 50건)
// 동작:
//   1) 종합 통계 계산 및 표시
//   2) 스테이션별 통계 계산 및 표시 (목업 대비 추가)
//   3) NG 이력 리스트 갱신
void CPageHome::Update(const std::vector<InspectionRecord>& recs)
{
    // ── 1) 종합 통계 계산 ──
    int total = (int)recs.size();
    int ng = 0;
    // 스테이션별 카운트
    int s1Total = 0, s1Ng = 0;
    int s2Total = 0, s2Ng = 0;

    for (const auto& r : recs) {
        if (r.isNG) ++ng;
        if (r.station == 1) {
            ++s1Total;
            if (r.isNG) ++s1Ng;
        } else if (r.station == 2) {
            ++s2Total;
            if (r.isNG) ++s2Ng;
        }
    }
    int ok = total - ng;

    // 컨트롤에 값 설정하는 헬퍼 람다
    auto set = [&](int id, CString v) {
        CWnd* w = GetDlgItem(id);
        if (w) w->SetWindowText(v);
    };

    CString s;
    // ── 종합 현황 ──
    s.Format(_T("%d"), total);   set(IDC_STATIC_TOTAL, s);
    s.Format(_T("%d"), ok);      set(IDC_STATIC_OK, s);
    s.Format(_T("%d"), ng);      set(IDC_STATIC_NG, s);
    s.Format(_T("%.2f%%"), total > 0 ? 100.0 * ng / total : 0.0);
    set(IDC_STATIC_DEFECT_RATE, s);
    set(IDC_STATIC_UPTIME, _T("98.7%"));

    // ── 2) 스테이션별 통계 (목업의 ①입고 / ②조립 개별 박스) ──
    s.Format(_T("%d"), s1Total - s1Ng);  set(IDC_STATIC_S1_OK, s);
    s.Format(_T("%d"), s1Ng);            set(IDC_STATIC_S1_NG, s);
    s.Format(_T("%d"), s2Total - s2Ng);  set(IDC_STATIC_S2_OK, s);
    s.Format(_T("%d"), s2Ng);            set(IDC_STATIC_S2_NG, s);

    // 스테이션별 모델 정보 표시
    set(IDC_STATIC_S1_MODEL_INFO, _T("모델: PatchCore v1.2.0 | Latency: ~52ms"));
    set(IDC_STATIC_S2_MODEL_INFO, _T("모델: YOLO11 v1.0.0 + PatchCore v1.1.0"));

    // ── 3) NG 이력 리스트 갱신 ──
    m_listNG.DeleteAllItems();
    int row = 0;
    // 역순(최신순)으로 NG만 표시
    for (int i = (int)recs.size() - 1; i >= 0; --i) {
        const auto& r = recs[i];
        if (!r.isNG) continue;  // OK는 건너뜀

        s.Format(_T("%d"), r.id);
        m_listNG.InsertItem(row, s);

        s.Format(_T("#%d"), r.station);
        m_listNG.SetItemText(row, 1, s);

        m_listNG.SetItemText(row, 2, r.time);
        m_listNG.SetItemText(row, 3, _T("NG"));

        s.Format(_T("%.2f"), r.score);
        m_listNG.SetItemText(row, 4, s);

        m_listNG.SetItemText(row, 5, QCUtil::DefectName(r.defect));

        s.Format(_T("%dms"), r.latencyMs);
        m_listNG.SetItemText(row, 6, s);

        ++row;
    }
}

void CPageHome::UpdateStationCount(int stationId, int okCount, int ngCount)
{
    auto set = [&](int id, CString v) {
        CWnd* w = GetDlgItem(id);
        if (w) w->SetWindowText(v);
    };
    CString s;
    if (stationId == 1) {
        s.Format(_T("%d"), okCount);  set(IDC_STATIC_S1_OK, s);
        s.Format(_T("%d"), ngCount);  set(IDC_STATIC_S1_NG, s);
    } else if (stationId == 2) {
        s.Format(_T("%d"), okCount);  set(IDC_STATIC_S2_OK, s);
        s.Format(_T("%d"), ngCount);  set(IDC_STATIC_S2_NG, s);
    }
}

void CPageHome::OnPaint() { Default(); }

// ============================================================================
// UpdateStationCount — 서버 OK/NG 카운트 푸시(프로토콜 112) 실시간 반영
// ============================================================================
// MainTabDlg::OnNetOkCountPush()에서 호출됩니다.
// 서버가 5초마다 보내는 누적 카운트를 홈 페이지의 스테이션 박스에 즉시 표시합니다.
void CPageHome::UpdateStationCount(int stationId, int okCount, int ngCount)
{
    CString s;
    if (stationId == 1) {
        s.Format(_T("%d"), okCount); 
        CWnd* w = GetDlgItem(IDC_STATIC_S1_OK);
        if (w) w->SetWindowText(s);
        s.Format(_T("%d"), ngCount);
        w = GetDlgItem(IDC_STATIC_S1_NG);
        if (w) w->SetWindowText(s);
    } else if (stationId == 2) {
        s.Format(_T("%d"), okCount);
        CWnd* w = GetDlgItem(IDC_STATIC_S2_OK);
        if (w) w->SetWindowText(s);
        s.Format(_T("%d"), ngCount);
        w = GetDlgItem(IDC_STATIC_S2_NG);
        if (w) w->SetWindowText(s);
    }
}
