// ============================================================================
// PageStats.cpp — 통계/이력 조회 페이지
// ============================================================================
// 책임:
//   기간/스테이션 필터로 과거 검사 이력 조회 + 테이블 표시.
//   - CSV 내보내기 (OnBtnExportCSV)
//
// v0.16.0 변경:
//   - 시간대별 추세선 / 파레토 차트 / 레이턴시 분포 차트 제거
//     (rand() 더미 데이터 기반, 실데이터 미연동으로 혼동 유발)
//   - 하단 이력 테이블(CListCtrl) 추가 — 서버 응답 실데이터 표시
//   - OnBtnQuery 미연결 시 더미 Rebuild() 제거 → 경고 메시지로 교체
// ============================================================================
#include "pch.h"
#include "PageStats.h"
#include "PacketBuilder.h"
#include <algorithm>

IMPLEMENT_DYNAMIC(CPageStats, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStats, CDialogEx)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_BN_CLICKED(IDC_BTN_QUERY,      OnBtnQuery)
    ON_BN_CLICKED(IDC_BTN_EXPORT_CSV, OnBtnExportCSV)
END_MESSAGE_MAP()

CPageStats::CPageStats(CWnd* p) : CDialogEx(IDD_PAGE_STATS, p) {}

void CPageStats::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST_HISTORY, m_listHistory);
}

BOOL CPageStats::OnInitDialog() {
    CDialogEx::OnInitDialog();

    // 이력 테이블 컬럼 설정
    m_listHistory.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listHistory.InsertColumn(0, _T("ID"),       LVCFMT_RIGHT,  60);
    m_listHistory.InsertColumn(1, _T("스테이션"), LVCFMT_CENTER, 70);
    m_listHistory.InsertColumn(2, _T("시각"),     LVCFMT_LEFT,   80);
    m_listHistory.InsertColumn(3, _T("결과"),     LVCFMT_CENTER, 50);
    m_listHistory.InsertColumn(4, _T("점수"),     LVCFMT_RIGHT,  60);
    m_listHistory.InsertColumn(5, _T("결함"),     LVCFMT_LEFT,  120);
    m_listHistory.InsertColumn(6, _T("Latency"),  LVCFMT_RIGHT,  70);

    return TRUE;
}

// v0.16.0: 차트 제거 — OnPaint 는 단순 기본 동작만
void CPageStats::OnPaint() { Default(); }
BOOL CPageStats::OnEraseBkgnd(CDC* /*pDC*/) { return TRUE; }

// Update: MainTabDlg PushUpdate() 호출 시 수신
void CPageStats::Update(const std::vector<InspectionRecord>& recs) {
    m_recs = recs;
    RefreshList();
}

// RefreshList: m_recs 를 이력 테이블에 표시
void CPageStats::RefreshList() {
    m_listHistory.DeleteAllItems();
    int row = 0;
    for (const auto& r : m_recs) {
        CString s;
        s.Format(_T("%d"), r.id);
        m_listHistory.InsertItem(row, s);

        s.Format(_T("#%d"), r.station);
        m_listHistory.SetItemText(row, 1, s);
        m_listHistory.SetItemText(row, 2, r.time);
        m_listHistory.SetItemText(row, 3, r.isNG ? _T("NG") : _T("OK"));

        s.Format(_T("%.2f"), r.score);
        m_listHistory.SetItemText(row, 4, s);

        m_listHistory.SetItemText(row, 5, QCUtil::DefectName(r.defect));

        s.Format(_T("%dms"), r.latencyMs);
        m_listHistory.SetItemText(row, 6, s);
        ++row;
    }
}

void CPageStats::OnBtnQuery() {
    if (m_net && m_net->IsConnected()) {
        CString histReq = CPacketBuilder::BuildInspectHistoryReq(
            0, _T(""), _T(""), 100);
        m_net->SendJson(histReq);
        CString statsReq = CPacketBuilder::BuildStatsReq(0, _T(""), _T(""));
        m_net->SendJson(statsReq);
    } else {
        // v0.16.0: 미연결 시 더미 데이터 제거 → 경고 안내
        MessageBox(_T("서버에 연결되어 있지 않습니다.\n연결 후 다시 조회하세요."),
                   _T("통계 조회"), MB_OK | MB_ICONWARNING);
    }
}

void CPageStats::OnBtnExportCSV() {
    CFileDialog dlg(FALSE, _T("csv"), _T("log.csv"),
                    OFN_OVERWRITEPROMPT, _T("CSV|*.csv||"), this);
    if (dlg.DoModal() != IDOK) return;
    CStdioFile f;
    if (!f.Open(dlg.GetPathName(),
                CFile::modeCreate | CFile::modeWrite | CFile::typeText)) {
        MessageBox(_T("파일 열기 실패"), _T("오류"), MB_OK | MB_ICONERROR);
        return;
    }
    f.WriteString(_T("ID,스테이션,시각,결과,점수,결함,Latency\n"));
    for (auto& r : m_recs) {
        CString l;
        l.Format(_T("%d,%d,%s,%s,%.2f,%s,%dms\n"),
                 r.id, r.station, (LPCTSTR)r.time,
                 r.isNG ? _T("NG") : _T("OK"),
                 r.score, (LPCTSTR)QCUtil::DefectName(r.defect), r.latencyMs);
        f.WriteString(l);
    }
    f.Close();
    MessageBox(_T("CSV 내보내기 완료"), _T("완료"), MB_OK | MB_ICONINFORMATION);
}

// ============================================================================
// OnInspectHistoryRes — 검사 이력 응답 수신 (프로토콜 115)
// ============================================================================
void CPageStats::OnInspectHistoryRes(const std::string& json)
{
    CStringA jsonA(json.c_str());
    int arrStart = jsonA.Find("[");
    int arrEnd   = jsonA.ReverseFind(']');
    if (arrStart < 0 || arrEnd < 0) return;

    CStringA arrStr = jsonA.Mid(arrStart + 1, arrEnd - arrStart - 1);
    m_recs.clear();

    int pos = 0;
    while (pos < arrStr.GetLength()) {
        int objStart = arrStr.Find('{', pos);
        int objEnd   = arrStr.Find('}', objStart);
        if (objStart < 0 || objEnd < 0) break;

        CStringA obj = arrStr.Mid(objStart, objEnd - objStart + 1);

        InspectionRecord rec;
        rec.id        = CPacketBuilder::ExtractInt(obj, "id");
        rec.station   = CPacketBuilder::ExtractInt(obj, "station_id");
        rec.score     = CPacketBuilder::ExtractDouble(obj, "confidence");
        rec.latencyMs = CPacketBuilder::ExtractInt(obj, "latency_ms");

        CStringA resultA = CPacketBuilder::ExtractString(obj, "result");
        rec.isNG = (resultA.CompareNoCase("ng") == 0);

        CStringA tsA = CPacketBuilder::ExtractString(obj, "timestamp");
        rec.time = (tsA.GetLength() >= 19) ? CString(tsA.Mid(11, 8)) : _T("--:--:--");

        CStringA defectA = CPacketBuilder::ExtractString(obj, "defect_type");
        if      (defectA == "anomaly")      rec.defect = EDefect::Anomaly;
        else if (defectA == "cap_loose")    rec.defect = EDefect::CapLoose;
        else if (defectA == "cap_missing")  rec.defect = EDefect::CapMissing;
        else if (defectA == "label_tilt")   rec.defect = EDefect::LabelTilt;
        else if (defectA == "label_torn")   rec.defect = EDefect::LabelTorn;
        else if (defectA == "fill_low")     rec.defect = EDefect::FillLow;
        else                                rec.defect = EDefect::Anomaly;

        m_recs.push_back(rec);
        pos = objEnd + 1;
    }

    RefreshList();
    TRACE(_T("[PageStats] 이력 수신: %d건\n"), (int)m_recs.size());
}

// ============================================================================
// RequestInspectionImage — 이력 이미지 on-demand 요청 (프로토콜 116)
// ============================================================================
void CPageStats::RequestInspectionImage(int inspectionId)
{
    if (!m_net || !m_net->IsConnected()) return;
    if (inspectionId <= 0) return;
    m_net->SendJson(CPacketBuilder::BuildInspectImageReq(inspectionId));
    TRACE(_T("[PageStats] 이력 이미지 요청 송신 | id=%d\n"), inspectionId);
}

int CPageStats::GetLastNgInspectionId() const {
    for (const auto& r : m_recs) if (r.isNG) return r.id;
    return 0;
}

int CPageStats::GetLastNgInspectionIdByStation(int station) const {
    for (const auto& r : m_recs) if (r.station == station && r.isNG)  return r.id;
    for (const auto& r : m_recs) if (r.station == station)             return r.id;
    return 0;
}

std::vector<int> CPageStats::GetRecentInspectionIdsByStation(int station, int count) const {
    std::vector<int> out;
    if (count <= 0) return out;
    out.reserve(count);
    for (const auto& r : m_recs) {
        if (r.station == station && r.isNG) {
            out.push_back(r.id);
            if ((int)out.size() >= count) return out;
        }
    }
    for (const auto& r : m_recs) {
        if (r.station == station && !r.isNG) {
            out.push_back(r.id);
            if ((int)out.size() >= count) return out;
        }
    }
    return out;
}

bool CPageStats::LookupInspectionMeta(int id, CString& outTime, double& outScore) const {
    for (const auto& r : m_recs) {
        if (r.id == id) { outTime = r.time; outScore = r.score; return true; }
    }
    return false;
}

// ============================================================================
// OnStatsRes — 통계 응답 수신 (프로토콜 131)
// ============================================================================
void CPageStats::OnStatsRes(const std::string& json)
{
    CStringA jsonA(json.c_str());
    int total   = CPacketBuilder::ExtractInt(jsonA, "total");
    int okCount = CPacketBuilder::ExtractInt(jsonA, "ok_count");
    int ngCount = CPacketBuilder::ExtractInt(jsonA, "ng_count");
    double ngRate = CPacketBuilder::ExtractDouble(jsonA, "ng_rate");
    int s1_ng   = CPacketBuilder::ExtractInt(jsonA, "s1_ng");
    int s2_ng   = CPacketBuilder::ExtractInt(jsonA, "s2_ng");
    int avgLat  = CPacketBuilder::ExtractInt(jsonA, "avg_latency_ms");
    TRACE(_T("[PageStats] 통계 수신: total=%d OK=%d NG=%d (%.2f%%) s1_ng=%d s2_ng=%d avg=%dms\n"),
          total, okCount, ngCount, ngRate, s1_ng, s2_ng, avgLat);
}
