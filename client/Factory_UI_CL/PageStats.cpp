#include "pch.h"
#include "PageStats.h"
#include "PacketBuilder.h"
#include <algorithm>

IMPLEMENT_DYNAMIC(CPageStats, CDialogEx)
BEGIN_MESSAGE_MAP(CPageStats, CDialogEx)
    ON_WM_PAINT()
    ON_BN_CLICKED(IDC_BTN_QUERY,      OnBtnQuery)
    ON_BN_CLICKED(IDC_BTN_EXPORT_CSV, OnBtnExportCSV)
END_MESSAGE_MAP()

CPageStats::CPageStats(CWnd* p) : CDialogEx(IDD_PAGE_STATS,p) {}
void CPageStats::DoDataExchange(CDataExchange* pDX){CDialogEx::DoDataExchange(pDX);}
BOOL CPageStats::OnInitDialog(){CDialogEx::OnInitDialog();Rebuild();return TRUE;}

void CPageStats::Update(const std::vector<InspectionRecord>& recs){m_recs=recs;Rebuild();Invalidate();}

void CPageStats::Rebuild(){
    m_trend.clear();
    for(int h=8;h<20;++h){
        TPoint p;p.lbl.Format(_T("%d:00"),h);
        p.s1=(rand()%150)/100.0;p.s2=(rand()%200)/100.0;p.lat=40+rand()%60;
        m_trend.push_back(p);
    }
    m_pareto={{_T("라벨기울어짐"),5},{_T("캡미체결"),3},{_T("이물질"),2},{_T("크랙"),1},{_T("미충전"),1}};
}

void CPageStats::OnPaint(){
    CPaintDC dc(this);
    CRect cr; GetClientRect(&cr);
    int mg=6, top=36, ch=160, lh=110;
    int hw=(cr.Width()-mg*3)/2;
    CRect trendRc(mg,top,mg+hw,top+ch);
    CRect paretoRc(mg*2+hw,top,cr.right-mg,top+ch);
    CRect latRc(mg,top+ch+mg,cr.right-mg,top+ch+mg+lh);
    DrawTrend(dc,trendRc);
    DrawPareto(dc,paretoRc);
    DrawLatency(dc,latRc);
}

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

void CPageStats::OnInspectHistoryRes(const std::string& json)
{
    // 서버에서 받은 검사 이력을 m_recs에 반영
    CStringA jsonA(json.c_str());
    int count = CPacketBuilder::ExtractInt(jsonA, "count");
    TRACE(_T("[PageStats] 검사 이력 수신: %d건\n"), count);
    // items 배열은 간이 파서로 처리 불가 — 수신 확인만 로그
    // TODO: JSON 배열 파서 추가 시 m_recs에 반영
    Rebuild();
    Invalidate();
}

void CPageStats::OnStatsRes(const std::string& json)
{
    // 서버에서 받은 통계를 표시
    CStringA jsonA(json.c_str());
    int total    = CPacketBuilder::ExtractInt(jsonA, "total");
    int okCount  = CPacketBuilder::ExtractInt(jsonA, "ok_count");
    int ngCount  = CPacketBuilder::ExtractInt(jsonA, "ng_count");
    double ngRate = CPacketBuilder::ExtractDouble(jsonA, "ng_rate");

    TRACE(_T("[PageStats] 통계 수신: total=%d ok=%d ng=%d rate=%.2f%%\n"),
        total, okCount, ngCount, ngRate);

    // TODO: 차트에 반영
    Rebuild();
    Invalidate();
}

void CPageStats::OnBtnQuery(){Rebuild();Invalidate();}
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
