#include "pch.h"
#include "CameraView.h"

// ── CCameraView ──────────────────────────────────────────────────────────────
IMPLEMENT_DYNAMIC(CCameraView, CStatic)
BEGIN_MESSAGE_MAP(CCameraView, CStatic)
    ON_WM_PAINT()
END_MESSAGE_MAP()

CCameraView::CCameraView()
    : m_station(1), m_isNG(false), m_score(0.1), m_defect(EDefect::None), m_flash(false) {}

void CCameraView::SetInspection(int st, bool ng, double sc, EDefect def) {
    m_station = st; m_isNG = ng; m_score = sc; m_defect = def;
    Invalidate();
}
void CCameraView::Tick() { m_flash = !m_flash; if (m_isNG) Invalidate(); }

void CCameraView::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);
    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* pOld = mem.SelectObject(&bmp);
    DrawBg(mem, rc);
    if (m_station == 2) DrawYolo(mem, rc);
    if (m_isNG) DrawNgBox(mem, rc);
    DrawBadge(mem, rc);
    DrawScoreBar(mem, rc);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(pOld);
}

void CCameraView::DrawBg(CDC& dc, CRect& rc) {
    dc.FillSolidRect(&rc, RGB(17,17,17));
    // 외곽선
    COLORREF bc = m_isNG ? RGB(200,0,0) : RGB(68,68,68);
    CPen pen(PS_SOLID, 2, bc); CPen* p = dc.SelectObject(&pen);
    CBrush* pb = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);
    dc.SelectObject(p); dc.SelectObject(pb);
    // 내부 박스
    CRect inner(rc.left+8, rc.top+14, rc.right-8, rc.bottom-32);
    dc.FillSolidRect(&inner, RGB(30,45,60));
    // 레이블
    dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(70,90,110));
    CFont f; f.CreatePointFont(70, _T("Courier New"));
    CFont* pf = dc.SelectObject(&f);
    CString lbl; lbl.Format(_T("Camera #%d"), m_station);
    dc.DrawText(lbl, &inner, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    // 크로스헤어
    int cx=rc.Width()/2, cy=rc.Height()/2-10;
    CPen cp(PS_SOLID,1,RGB(50,150,80)); CPen* cp2=dc.SelectObject(&cp);
    dc.MoveTo(cx-8,cy); dc.LineTo(cx+8,cy);
    dc.MoveTo(cx,cy-8); dc.LineTo(cx,cy+8);
    dc.SelectObject(cp2); dc.SelectObject(pf);
}

void CCameraView::DrawYolo(CDC& dc, CRect& rc) {
    int W=rc.Width(), H=rc.Height();
    struct Box { float x,y,w,h; COLORREF c; LPCTSTR lbl; };
    Box boxes[]={
        {0.30f,0.09f,0.40f,0.09f,RGB(80,220,80),_T("cap 0.94")},
        {0.18f,0.29f,0.65f,0.29f,RGB(80,130,240),_T("label 0.91")},
        {0.22f,0.61f,0.55f,0.14f,RGB(240,130,60),_T("liquid 0.95")},
    };
    CFont f; f.CreatePointFont(60,_T("Tahoma"));
    CFont* pf=dc.SelectObject(&f);
    dc.SetBkMode(TRANSPARENT);
    for (auto& b : boxes) {
        CRect br((int)(W*b.x),(int)(H*b.y),(int)(W*(b.x+b.w)),(int)(H*(b.y+b.h)));
        CPen pen(PS_SOLID,1,b.c); CPen* pp=dc.SelectObject(&pen);
        CBrush* pb=(CBrush*)dc.SelectStockObject(NULL_BRUSH);
        dc.Rectangle(&br); dc.SelectObject(pp); dc.SelectObject(pb);
        dc.SetTextColor(b.c);
        CRect tr(br.left, br.top-12, br.left+80, br.top);
        dc.DrawText(b.lbl, &tr, DT_LEFT|DT_SINGLELINE);
    }
    dc.SelectObject(pf);
}

void CCameraView::DrawNgBox(CDC& dc, CRect& rc) {
    if (!m_flash) return;
    int W=rc.Width(), H=rc.Height();
    CRect nr((int)(W*0.27f),(int)(H*0.25f),(int)(W*0.53f),(int)(H*0.40f));
    CPen pen(PS_DASH,2,RGB(255,60,60)); CPen* pp=dc.SelectObject(&pen);
    CBrush* pb=(CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&nr); dc.SelectObject(pp); dc.SelectObject(pb);
    dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(255,140,140));
    CFont f; f.CreatePointFont(65,_T("Tahoma")); CFont* pf=dc.SelectObject(&f);
    CString lbl = (m_station==1) ? _T("이상 영역") : QCUtil::DefectName(m_defect);
    CRect tr(nr.left, nr.top-12, nr.left+120, nr.top);
    dc.DrawText(lbl, &tr, DT_LEFT|DT_SINGLELINE);
    dc.SelectObject(pf);
}

void CCameraView::DrawBadge(CDC& dc, CRect& rc) {
    COLORREF bg = m_isNG ? RGB(180,0,0) : RGB(0,110,0);
    int m=5, h=24;
    CRect br(rc.left+m, rc.bottom-h-m, rc.right-m, rc.bottom-m);
    CBrush b(bg); CPen p(PS_SOLID,1,bg);
    CPen* pp=dc.SelectObject(&p); CBrush* pb=dc.SelectObject(&b);
    dc.RoundRect(br.left,br.top,br.right,br.bottom,6,6); dc.SelectObject(pp); dc.SelectObject(pb);
    dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(255,255,255));
    CFont f; f.CreatePointFont(120,_T("Tahoma"));
    LOGFONT lf; f.GetLogFont(&lf); lf.lfWeight=FW_BOLD;
    CFont bf; bf.CreateFontIndirect(&lf); CFont* pf=dc.SelectObject(&bf);
    dc.DrawText(m_isNG?_T("NG"):_T("OK"), &br, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    dc.SelectObject(pf);
}

void CCameraView::DrawScoreBar(CDC& dc, CRect& rc) {
    CRect bar(rc.left,rc.top,rc.right,rc.top+14);
    CBrush b(RGB(0,0,0)); dc.FillRect(&bar,&b);
    dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(130,180,255));
    CFont f; f.CreatePointFont(60,_T("Courier New")); CFont* pf=dc.SelectObject(&f);
    CString s; s.Format(_T("Score:%.2f"),m_score);
    CRect tl(rc.left+2,rc.top+1,rc.left+90,rc.top+13);
    dc.DrawText(s,&tl,DT_LEFT|DT_SINGLELINE);
    dc.SelectObject(pf);
}

// ── CHeatmapView ─────────────────────────────────────────────────────────────
IMPLEMENT_DYNAMIC(CHeatmapView, CStatic)
BEGIN_MESSAGE_MAP(CHeatmapView, CStatic)
    ON_WM_PAINT()
END_MESSAGE_MAP()

CHeatmapView::CHeatmapView() : m_active(false) {}
void CHeatmapView::SetActive(bool a) { m_active=a; Invalidate(); }

void CHeatmapView::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);
    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc,rc.Width(),rc.Height());
    CBitmap* pOld=mem.SelectObject(&bmp);
    mem.FillSolidRect(&rc,RGB(17,17,17));
    int W=rc.Width(), H=rc.Height();
    CRect bottle((int)(W*0.15),(int)(H*0.05),(int)(W*0.85),(int)(H*0.93));
    CPen pen(PS_SOLID,1,RGB(50,70,110)); CPen* pp=mem.SelectObject(&pen);
    CBrush br(RGB(25,40,65)); CBrush* pb=mem.SelectObject(&br);
    mem.RoundRect(bottle.left,bottle.top,bottle.right,bottle.bottom,10,10); mem.SelectObject(pp); mem.SelectObject(pb);
    // 핫스팟
    int r1=m_active?(int)(H*0.20):(int)(H*0.13);
    int r2=m_active?(int)(H*0.14):(int)(H*0.09);
    COLORREF c1=m_active?RGB(220,0,0):RGB(0,110,0);
    COLORREF c2=m_active?RGB(255,160,0):RGB(0,80,0);
    auto drawHot=[&](int cx,int cy,int r,COLORREF c){
        for(int i=r;i>0;i-=3){
            BYTE R=GetRValue(c),G=GetGValue(c),B=GetBValue(c);
            COLORREF bc=RGB(R+(17-R)*i/r,G+(17-G)*i/r,B+(17-B)*i/r);
            CBrush hb(bc); CPen hp(PS_SOLID,1,bc);
            CPen* p1=mem.SelectObject(&hp); CBrush* p2=mem.SelectObject(&hb);
            mem.Ellipse(cx-i,cy-i,cx+i,cy+i);
            mem.SelectObject(p1); mem.SelectObject(p2);
        }
    };
    drawHot((int)(W*0.45),(int)(H*0.40),r1,c1);
    drawHot((int)(W*0.57),(int)(H*0.65),r2,c2);
    mem.SetBkMode(TRANSPARENT); mem.SetTextColor(RGB(100,100,100));
    CFont f; f.CreatePointFont(60,_T("Tahoma")); CFont* pf=mem.SelectObject(&f);
    CRect lr(rc.left,rc.bottom-13,rc.right,rc.bottom);
    mem.DrawText(_T("Anomaly Heatmap"),&lr,DT_CENTER|DT_SINGLELINE);
    mem.SelectObject(pf);
    dc.BitBlt(0,0,rc.Width(),rc.Height(),&mem,0,0,SRCCOPY);
    mem.SelectObject(pOld);
}
