// ============================================================================
// PageStats.cpp — 통계/이력 조회 페이지
// ============================================================================
// 책임:
//   기간/스테이션 필터로 과거 검사 이력 조회 + 그래프/테이블 표시.
//   - 시간대별 OK/NG 추세선
//   - 결함 유형별 파레토 차트
//   - 레이턴시 분포
//   - CSV 내보내기 (OnBtnExportCSV)
//
// 데이터 요청:
//   OnBtnQuery → STATS_REQ(130) + INSPECT_HISTORY_REQ(114) 동시 전송
//   두 응답이 모두 도착하면 Rebuild() 로 차트/테이블 재구성.
// ============================================================================
#include "pch.h"
#include "PageStats.h"
#include "PacketBuilder.h"   // ExtractInt/String/Double
#include <algorithm>

IMPLEMENT_DYNAMIC(CPageStats, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStats, CDialogEx)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()   // v0.14.6: 배경 지움 억제해 깜빡임 차단
    ON_BN_CLICKED(IDC_BTN_QUERY,      OnBtnQuery)
    ON_BN_CLICKED(IDC_BTN_EXPORT_CSV, OnBtnExportCSV)
END_MESSAGE_MAP()

CPageStats::CPageStats(CWnd* p) : CDialogEx(IDD_PAGE_STATS,p) {}
void CPageStats::DoDataExchange(CDataExchange* pDX){CDialogEx::DoDataExchange(pDX);}
BOOL CPageStats::OnInitDialog(){CDialogEx::OnInitDialog();Rebuild();return TRUE;}

void CPageStats::Update(const std::vector<InspectionRecord>& recs){m_recs=recs;Rebuild();Invalidate();}

void CPageStats::Rebuild(){
    // v0.15.0: rand() 기반 더미 추세 데이터 및 하드코딩 파레토 항목 제거.
    // m_trend / m_pareto 는 OnStatsRes() / OnInspectHistoryRes() 가
    // 서버 응답을 받은 후에만 채워진다.
    // 서버 데이터가 없는 상태에서는 빈 차트를 표시.
}

// v0.14.6: 통계 차트 영역 더블버퍼링 — 갱신 시 깜빡임 제거.
void CPageStats::OnPaint(){
    CPaintDC dc(this);
    CRect cr; GetClientRect(&cr);

    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, cr.Width(), cr.Height());
    CBitmap* pOld = mem.SelectObject(&bmp);

    // 다이얼로그 기본 배경색으로 채우고 그 위에 차트 렌더링
    mem.FillSolidRect(&cr, ::GetSysColor(COLOR_BTNFACE));

    int mg=6, top=36, ch=160, lh=110;
    int hw=(cr.Width()-mg*3)/2;
    CRect trendRc(mg,top,mg+hw,top+ch);
    CRect paretoRc(mg*2+hw,top,cr.right-mg,top+ch);
    CRect latRc(mg,top+ch+mg,cr.right-mg,top+ch+mg+lh);
    DrawTrend(mem,trendRc);
    DrawPareto(mem,paretoRc);
    DrawLatency(mem,latRc);

    dc.BitBlt(0, 0, cr.Width(), cr.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(pOld);
}

// v0.14.6: 배경 지움 억제 — OnPaint 가 더블버퍼 안에서 배경까지 그림.
BOOL CPageStats::OnEraseBkgnd(CDC* /*pDC*/) { return TRUE; }

void CPageStats::DrawGrid(CDC& dc, CRect rc, int rows, int cols){
    CPen gp(PS_SOLID,1,RGB(204,204,204)); CPen* p=dc.SelectObject(&gp);
    for(int i=1;i<rows;++i){int y=rc.top+rc.Height()*i/rows;dc.MoveTo(rc.left,y);dc.LineTo(rc.right,y);}
    for(int i=1;i<cols;++i){int x=rc.left+rc.Width()*i/cols;dc.MoveTo(x,rc.top);dc.LineTo(x,rc.bottom);}
    dc.SelectObject(p);
}

void CPageStats::DrawTrend(CDC& dc, CRect rc){
    dc.FillSolidRect(&rc,RGB(255,255,255));
    CPen bp(PS_SOLID,1,QCUtil::ColBorder());CPen* pp=dc.SelectObject(&bp);
    CBrush* pb=(CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);dc.SelectObject(pp);dc.SelectObject(pb);
    DrawGrid(dc,rc,4,(int)m_trend.size());
    if(m_trend.empty())return;
    int n=(int)m_trend.size(); double maxR=2.5;
    CRect pl(rc.left+28,rc.top+4,rc.right-4,rc.bottom-14);
    auto pt=[&](int i,double v)->CPoint{
        return CPoint(pl.left+(int)((double)i/(n-1)*pl.Width()),
                      pl.bottom-(int)(v/maxR*pl.Height()));};
    {CPen lp(PS_SOLID,2,RGB(0,112,192));dc.SelectObject(&lp);
    dc.MoveTo(pt(0,m_trend[0].s1));for(int i=1;i<n;++i)dc.LineTo(pt(i,m_trend[i].s1));}
    {CPen lp(PS_SOLID,2,RGB(204,0,0));dc.SelectObject(&lp);
    dc.MoveTo(pt(0,m_trend[0].s2));for(int i=1;i<n;++i)dc.LineTo(pt(i,m_trend[i].s2));}
    dc.SetBkMode(TRANSPARENT);dc.SetTextColor(RGB(80,80,80));
    CFont f;f.CreatePointFont(60,_T("Tahoma"));CFont* pf=dc.SelectObject(&f);
    CRect ll(rc.left+2,rc.top+2,rc.right,rc.top+12);
    dc.SetTextColor(RGB(0,112,192));
    CRect ll1(rc.right-110,rc.top+2,rc.right-60,rc.top+12);dc.DrawText(_T("■ 입고"),&ll1,DT_LEFT|DT_SINGLELINE);
    dc.SetTextColor(RGB(204,0,0));
    CRect ll2(rc.right-58,rc.top+2,rc.right-4,rc.top+12);dc.DrawText(_T("■ 조립"),&ll2,DT_LEFT|DT_SINGLELINE);
    dc.SelectObject(pf);
}

void CPageStats::DrawPareto(CDC& dc, CRect rc){
    dc.FillSolidRect(&rc,RGB(255,255,255));
    CPen bp(PS_SOLID,1,QCUtil::ColBorder());CPen* pp=dc.SelectObject(&bp);
    CBrush* pb=(CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);dc.SelectObject(pp);dc.SelectObject(pb);
    DrawGrid(dc,rc,4,(int)m_pareto.size());
    if(m_pareto.empty())return;
    static COLORREF cols[]={RGB(204,0,0),RGB(224,96,0),RGB(212,160,0),RGB(0,112,192),RGB(128,128,128)};
    int n=(int)m_pareto.size(),maxC=m_pareto[0].cnt;
    if(maxC<=0) maxC=1;  // 0 나눗셈 방지
    CRect pl(rc.left+28,rc.top+4,rc.right-4,rc.bottom-20);
    int bw=(pl.Width()/n)-4;
    dc.SetBkMode(TRANSPARENT);
    CFont f;f.CreatePointFont(60,_T("Tahoma"));CFont* pf=dc.SelectObject(&f);
    for(int i=0;i<n;++i){
        int bh=(int)((double)m_pareto[i].cnt/maxC*pl.Height());
        int x=pl.left+i*(bw+4);
        CRect bar(x,pl.bottom-bh,x+bw,pl.bottom);
        CBrush b(cols[i%5]);CPen p(PS_SOLID,1,cols[i%5]);
        CPen* p1=dc.SelectObject(&p);CBrush* p2=dc.SelectObject(&b);
        dc.Rectangle(&bar);dc.SelectObject(p1);dc.SelectObject(p2);
        dc.SetTextColor(RGB(0,0,0));
        CString s;s.Format(_T("%d"),m_pareto[i].cnt);
        CRect cr2(bar.left,bar.top-12,bar.right,bar.top);
        dc.DrawText(s,&cr2,DT_CENTER|DT_SINGLELINE);
        dc.SetTextColor(RGB(80,80,80));
        CRect xr(bar.left-2,pl.bottom+2,bar.right+2,pl.bottom+14);
        dc.DrawText(m_pareto[i].name,&xr,DT_CENTER|DT_SINGLELINE);
    }
    dc.SelectObject(pf);
}

void CPageStats::DrawLatency(CDC& dc, CRect rc){
    dc.FillSolidRect(&rc,RGB(255,255,255));
    CPen bp(PS_SOLID,1,QCUtil::ColBorder());CPen* pp=dc.SelectObject(&bp);
    CBrush* pb=(CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);dc.SelectObject(pp);dc.SelectObject(pb);
    DrawGrid(dc,rc,3,10);
    if(m_trend.size()<2)return;
    int n=(int)m_trend.size();double maxMs=120.0;
    CRect pl(rc.left+28,rc.top+4,rc.right-4,rc.bottom-10);
    CPen lp(PS_SOLID,2,RGB(138,43,226));dc.SelectObject(&lp);
    auto pt=[&](int i,double v)->CPoint{
        return CPoint(pl.left+(int)((double)i/(n-1)*pl.Width()),
                      pl.bottom-(int)(v/maxMs*pl.Height()));};
    dc.MoveTo(pt(0,m_trend[0].lat));
    for(int i=1;i<n;++i)dc.LineTo(pt(i,m_trend[i].lat));
}

void CPageStats::OnBtnQuery(){
    // 서버에 검사 이력 + 통계 요청 전송
    if (m_net && m_net->IsConnected()) {
        CString histReq = CPacketBuilder::BuildInspectHistoryReq(
            0, _T(""), _T(""), 100);  // 전체 스테이션, 날짜 필터 없음, 최대 100건
        m_net->SendJson(histReq);

        CString statsReq = CPacketBuilder::BuildStatsReq(0, _T(""), _T(""));
        m_net->SendJson(statsReq);
    } else {
        // v0.15.0: 서버 미연결 시 더미 Rebuild() 제거 → 경고 안내
        MessageBox(_T("서버에 연결되어 있지 않습니다.\n연결 후 다시 조회하세요."),
                   _T("통계 조회"), MB_OK | MB_ICONWARNING);
    }
}
void CPageStats::OnBtnExportCSV(){
    CFileDialog dlg(FALSE,_T("csv"),_T("log.csv"),OFN_OVERWRITEPROMPT,_T("CSV|*.csv||"),this);
    if(dlg.DoModal()!=IDOK)return;
    CStdioFile f;
    if(!f.Open(dlg.GetPathName(),CFile::modeCreate|CFile::modeWrite|CFile::typeText)){
        MessageBox(_T("파일 열기 실패"),_T("오류"),MB_OK|MB_ICONERROR);return;}
    f.WriteString(_T("ID,스테이션,시각,결과,점수,결함,Latency\n"));
    for(auto& r:m_recs){
        CString l;l.Format(_T("%d,%d,%s,%s,%.2f,%s,%dms\n"),
            r.id,r.station,(LPCTSTR)r.time,r.isNG?_T("NG"):_T("OK"),
            r.score,(LPCTSTR)QCUtil::DefectName(r.defect),r.latencyMs);
        f.WriteString(l);}
    f.Close();
    MessageBox(_T("CSV 내보내기 완료"),_T("완료"),MB_OK|MB_ICONINFORMATION);
}

// ============================================================================
// OnInspectHistoryRes — 검사 이력 응답 수신 (프로토콜 115)
// ============================================================================
// MainTabDlg::OnNetResponse()에서 INSPECT_HISTORY_RES 수신 시 호출됩니다.
// JSON 배열 내 각 레코드를 파싱하여 m_recs에 추가하고 화면을 갱신합니다.
// 서버 JSON 예시:
//   {"protocol_no":115,"records":[
//     {"id":1,"station_id":1,"result":"NG","score":0.91,
//      "defect_type":"cap_loose","latency_ms":52,"timestamp":"2026-04-16T14:00:00"},
//     ...
//   ]}
void CPageStats::OnInspectHistoryRes(const std::string& json)
{
    CStringA jsonA(json.c_str());

    // records 배열을 수동 파싱합니다 (경량 파서 한계상 객체 단위로 분리)
    // "[" ~ "]" 구간을 추출하여 "}" 단위로 분리합니다.
    int arrStart = jsonA.Find("[");
    int arrEnd   = jsonA.ReverseFind(']');
    if (arrStart < 0 || arrEnd < 0) return;

    CStringA arrStr = jsonA.Mid(arrStart + 1, arrEnd - arrStart - 1);

    m_recs.clear();

    // "}" 기준으로 각 레코드 객체를 분리
    int pos = 0;
    while (pos < arrStr.GetLength()) {
        int objStart = arrStr.Find('{', pos);
        int objEnd   = arrStr.Find('}', objStart);
        if (objStart < 0 || objEnd < 0) break;

        CStringA obj = arrStr.Mid(objStart, objEnd - objStart + 1);

        InspectionRecord rec;
        rec.id        = CPacketBuilder::ExtractInt(obj, "id");
        rec.station   = CPacketBuilder::ExtractInt(obj, "station_id");
        rec.score     = CPacketBuilder::ExtractDouble(obj, "confidence");  // DB 컬럼명 기준
        rec.latencyMs = CPacketBuilder::ExtractInt(obj, "latency_ms");

        CStringA resultA  = CPacketBuilder::ExtractString(obj, "result");
        rec.isNG = (resultA.CompareNoCase("ng") == 0);  // 대소문자 무관 (서버는 "ng" 소문자)

        CStringA tsA = CPacketBuilder::ExtractString(obj, "timestamp");
        if (tsA.GetLength() >= 19)
            rec.time = CString(tsA.Mid(11, 8));  // "HH:MM:SS"
        else
            rec.time = _T("--:--:--");

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

    // 파레토/트렌드 재계산 후 화면 갱신
    Rebuild();
    Invalidate();

    TRACE(_T("[PageStats] 이력 수신: %d건\n"), (int)m_recs.size());
}

// ============================================================================
// RequestInspectionImage — 이력 이미지 on-demand 요청 (프로토콜 116)
// ============================================================================
// 서버는 INSPECT_IMAGE_RES(117)로 JSON + 3장 바이너리를 회신.
// NetworkClient가 자동으로 WM_NET_NG_IMAGE 메시지로 UI에 전달하므로
// 이 함수는 "요청만" 하면 됨. 결과는 PageStation1/2에서 자동 표시.
void CPageStats::RequestInspectionImage(int inspectionId)
{
    if (!m_net || !m_net->IsConnected()) {
        TRACE(_T("[PageStats] 이미지 요청 실패 — 네트워크 미연결\n"));
        return;
    }
    if (inspectionId <= 0) {
        TRACE(_T("[PageStats] 이미지 요청 실패 — 잘못된 id=%d\n"), inspectionId);
        return;
    }
    CString req = CPacketBuilder::BuildInspectImageReq(inspectionId);
    m_net->SendJson(req);
    TRACE(_T("[PageStats] 이력 이미지 요청 송신 | id=%d\n"), inspectionId);
}

// 최근 NG 이력 id — m_recs 는 OnInspectHistoryRes 에서 시간 역순으로 채워짐.
// 첫 NG 항목의 id 반환. 없으면 0.
int CPageStats::GetLastNgInspectionId() const
{
    for (const auto& r : m_recs) {
        if (r.isNG) return r.id;
    }
    return 0;
}

// 특정 스테이션의 최신 NG 이력 id — m_recs 는 시간 역순이므로 첫 매칭이 최신.
// NG가 없으면 해당 스테이션의 최신 OK 레코드로 폴백 (뭐라도 보여주기 위해).
int CPageStats::GetLastNgInspectionIdByStation(int station) const
{
    // 1순위: 해당 스테이션의 최신 NG
    for (const auto& r : m_recs) {
        if (r.station == station && r.isNG) return r.id;
    }
    // 2순위: 해당 스테이션의 최신 레코드 (OK도 포함)
    for (const auto& r : m_recs) {
        if (r.station == station) return r.id;
    }
    return 0;
}

// 특정 스테이션의 최신 inspection_id 리스트 (시간 역순, 최대 count개).
// NG 우선으로 채우고 부족하면 OK로 보충 — 그래야 리스트가 비어보이지 않음.
std::vector<int> CPageStats::GetRecentInspectionIdsByStation(int station, int count) const
{
    std::vector<int> out;
    if (count <= 0) return out;
    out.reserve(count);
    // 1순위: NG 우선 (시간 역순 유지)
    for (const auto& r : m_recs) {
        if (r.station == station && r.isNG) {
            out.push_back(r.id);
            if (static_cast<int>(out.size()) >= count) return out;
        }
    }
    // 2순위: OK로 보충
    for (const auto& r : m_recs) {
        if (r.station == station && !r.isNG) {
            out.push_back(r.id);
            if (static_cast<int>(out.size()) >= count) return out;
        }
    }
    return out;
}

// inspection_id → (time, score) 조회. m_recs는 로그인 직후 히스토리 응답으로 채워짐.
bool CPageStats::LookupInspectionMeta(int id, CString& outTime, double& outScore) const
{
    for (const auto& r : m_recs) {
        if (r.id == id) {
            outTime  = r.time;
            outScore = r.score;
            return true;
        }
    }
    return false;
}

// ============================================================================
// OnStatsRes — 통계 응답 수신 (프로토콜 131)
// ============================================================================
// 서버 실제 응답 구조 (gui_router.cpp handle_stats):
//   {"protocol_no":131,
//    "total":N, "ok_count":N, "ng_count":N, "ng_rate":%,
//    "s1_ok":N, "s1_ng":N, "s2_ok":N, "s2_ng":N,
//    "avg_latency_ms":ms}
//
// 클라이언트는 이 flat 구조를 받아 스테이션별 막대(파레토) +
// 시간대 트렌드(검사 이력으로부터 대체 계산)로 변환하여 표시한다.
void CPageStats::OnStatsRes(const std::string& json)
{
    CStringA jsonA(json.c_str());

    int total    = CPacketBuilder::ExtractInt(jsonA, "total");
    int okCount  = CPacketBuilder::ExtractInt(jsonA, "ok_count");
    int ngCount  = CPacketBuilder::ExtractInt(jsonA, "ng_count");
    double ngRate = CPacketBuilder::ExtractDouble(jsonA, "ng_rate");
    int s1_ok    = CPacketBuilder::ExtractInt(jsonA, "s1_ok");
    int s1_ng    = CPacketBuilder::ExtractInt(jsonA, "s1_ng");
    int s2_ok    = CPacketBuilder::ExtractInt(jsonA, "s2_ok");
    int s2_ng    = CPacketBuilder::ExtractInt(jsonA, "s2_ng");
    int avgLat   = CPacketBuilder::ExtractInt(jsonA, "avg_latency_ms");

    // 스테이션별 NG 카운트를 파레토 차트로 표시
    m_pareto.clear();
    if (s1_ng > 0) {
        PItem it; it.name = _T("입고 NG"); it.cnt = s1_ng;
        m_pareto.push_back(it);
    }
    if (s2_ng > 0) {
        PItem it; it.name = _T("조립 NG"); it.cnt = s2_ng;
        m_pareto.push_back(it);
    }

    Invalidate();
    TRACE(_T("[PageStats] 통계 수신: total=%d OK=%d NG=%d (%.2f%%) s1=%d/%d s2=%d/%d avg=%dms\n"),
          total, okCount, ngCount, ngRate, s1_ok, s1_ng, s2_ok, s2_ng, avgLat);
}
