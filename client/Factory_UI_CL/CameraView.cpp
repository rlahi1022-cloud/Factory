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
    // 외곽선 — OK/NG 무관하게 단색 유지
    CPen pen(PS_SOLID, 2, RGB(68,68,68)); CPen* p = dc.SelectObject(&pen);
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
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* pOld = mem.SelectObject(&bmp);
    // 배경만 채움 — 실서버 연동 시 수신한 히트맵 이미지를 BitBlt로 출력 예정
    mem.FillSolidRect(&rc, RGB(17, 17, 17));
    mem.SetBkMode(TRANSPARENT);
    mem.SetTextColor(RGB(100, 100, 100));
    CFont f; f.CreatePointFont(60, _T("Tahoma")); CFont* pf = mem.SelectObject(&f);
    CRect lr(rc.left, rc.bottom - 13, rc.right, rc.bottom);
    mem.DrawText(_T("Anomaly Heatmap"), &lr, DT_CENTER | DT_SINGLELINE);
    mem.SelectObject(pf);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(pOld);
}

// ── CPredMaskView ─────────────────────────────────────────────────────────────
IMPLEMENT_DYNAMIC(CPredMaskView, CStatic)
BEGIN_MESSAGE_MAP(CPredMaskView, CStatic)
    ON_WM_PAINT()
END_MESSAGE_MAP()

CPredMaskView::CPredMaskView()
    : m_active(false)
    , m_cx1(0.55), m_cy1(0.22)
    , m_cx2(0.52), m_cy2(0.52) {}

void CPredMaskView::SetMask(bool is_active,
                             double cx1, double cy1,
                             double cx2, double cy2) {
    m_active = is_active;
    m_cx1 = cx1; m_cy1 = cy1;
    m_cx2 = cx2; m_cy2 = cy2;
    Invalidate();
}

void CPredMaskView::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);
    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* p_old = mem.SelectObject(&bmp);
    draw_bg(mem, rc);
    if (m_active) draw_mask_circles(mem, rc);
    draw_label(mem, rc);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(p_old);
}

void CPredMaskView::draw_bg(CDC& dc, CRect& rc) {
    // 배경만 채움 — 실서버 연동 시 수신한 원본 이미지를 BitBlt로 출력 예정
    dc.FillSolidRect(&rc, RGB(17, 17, 17));
    // 외곽선 — OK/NG 무관하게 단색 유지
    CPen pen(PS_SOLID, 2, RGB(68, 68, 68));
    CPen* p_old_pen = dc.SelectObject(&pen);
    CBrush* p_old_br = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);
    dc.SelectObject(p_old_pen);
    dc.SelectObject(p_old_br);
}

void CPredMaskView::draw_mask_circles(CDC& dc, CRect& rc) {
    int W = rc.Width(), H = rc.Height();
    // 빨간 원 2개 — 이상 영역 마킹 (참조 이미지 기준)
    struct MaskCircle { double cx, cy; int radius; };
    MaskCircle circles[] = {
        { m_cx1, m_cy1, (int)(H * 0.09f) },  // 상단 이상 영역
        { m_cx2, m_cy2, (int)(H * 0.06f) },  // 중단 이상 영역
    };
    CPen red_pen(PS_SOLID, 2, RGB(255, 50, 50));
    CPen* p_old = dc.SelectObject(&red_pen);
    CBrush* p_old_br = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
    for (auto& c : circles) {
        int cx = (int)(W * c.cx);
        int cy = (int)(H * c.cy);
        int r  = c.radius;
        dc.Ellipse(cx - r, cy - r, cx + r, cy + r);
    }
    dc.SelectObject(p_old);
    dc.SelectObject(p_old_br);
}

void CPredMaskView::draw_label(CDC& dc, CRect& rc) {
    // 상단 스코어 바 (CCameraView와 동일 스타일)
    CRect bar(rc.left, rc.top, rc.right, rc.top + 14);
    CBrush b(RGB(0, 0, 0)); dc.FillRect(&bar, &b);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(130, 180, 255));
    CFont f; f.CreatePointFont(60, _T("Courier New"));
    CFont* p_old_f = dc.SelectObject(&f);
    CRect tl(rc.left + 2, rc.top + 1, rc.left + 110, rc.top + 13);
    dc.DrawText(_T("Pred Mask"), &tl, DT_LEFT | DT_SINGLELINE);
    dc.SelectObject(p_old_f);
    // 하단 레이블
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(100, 100, 100));
    CFont lf; lf.CreatePointFont(60, _T("Tahoma"));
    CFont* p_old_lf = dc.SelectObject(&lf);
    CRect lr(rc.left, rc.bottom - 13, rc.right, rc.bottom);
    dc.DrawText(_T("Pred Mask"), &lr, DT_CENTER | DT_SINGLELINE);
    dc.SelectObject(p_old_lf);
}
