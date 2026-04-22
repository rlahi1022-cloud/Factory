#include "pch.h"
#include "CameraView.h"

// ── 공통 이미지 디코더 ──────────────────────────────────────────────────────
// CImage::Load는 IStream 기반이므로, 바이트 벡터를 HGLOBAL에 복사 후 스트림 생성.
// 실패 시 out.Destroy()로 초기화된 상태를 보장.
namespace CameraViewUtil {

bool LoadImageFromBytes(const std::vector<BYTE>& bytes, CImage& out) {
    // Destroy 전에 DC가 열려있으면 안 되므로 IsNull 체크 후 안전하게 파괴
    // ReleaseDC()는 DC가 열린 상태에서만 호출해야 하므로 여기서는 호출하지 않음
    // → DC 열린 채 Destroy가 발생하는 근본 원인은 Paint 중 SetImage 호출이므로
    //   CRITICAL_SECTION으로 원천 차단함
    if (!out.IsNull()) {
        out.Destroy();
    }
    if (bytes.empty()) return false;

    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hMem) return false;

    {
        void* pMem = ::GlobalLock(hMem);
        if (!pMem) { ::GlobalFree(hMem); return false; }
        memcpy(pMem, bytes.data(), bytes.size());
        ::GlobalUnlock(hMem);
    }

    IStream* pStream = nullptr;
    // TRUE = 스트림 해제 시 HGLOBAL 자동 free
    if (::CreateStreamOnHGlobal(hMem, TRUE, &pStream) != S_OK || !pStream) {
        ::GlobalFree(hMem);
        return false;
    }

    HRESULT hr = out.Load(pStream);
    pStream->Release();  // HGLOBAL도 여기서 함께 해제

    if (FAILED(hr)) {
        out.Destroy();
        return false;
    }
    return true;
}

// StretchBlt 이미지 → 대상 사각형에 맞춰 그리기 (비율 유지 X, 단순 stretch)
static void DrawImageStretched(CImage& img, CDC& dc, const CRect& rc) {
    if (img.IsNull() || rc.Width() <= 0 || rc.Height() <= 0) return;
    // CImage::StretchBlt(HDC) 대신 명시적 GetDC/ReleaseDC 사용
    // — 더블버퍼 mem DC와 CImage 내부 DC 캐시 충돌 방지
    HDC hImgDC = img.GetDC();
    int oldMode = ::SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
    ::SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
    ::StretchBlt(dc.GetSafeHdc(),
                 rc.left, rc.top, rc.Width(), rc.Height(),
                 hImgDC,
                 0, 0, img.GetWidth(), img.GetHeight(),
                 SRCCOPY);
    ::SetStretchBltMode(dc.GetSafeHdc(), oldMode);
    img.ReleaseDC();
}

} // namespace CameraViewUtil

// ── CCameraView ──────────────────────────────────────────────────────────────
IMPLEMENT_DYNAMIC(CCameraView, CStatic)
BEGIN_MESSAGE_MAP(CCameraView, CStatic)
    ON_WM_PAINT()
END_MESSAGE_MAP()

CCameraView::CCameraView()
    : m_station(1), m_isNG(false), m_score(0.1), m_defect(EDefect::None), m_flash(false) {
    ::InitializeCriticalSection(&m_cs);
}

void CCameraView::SetInspection(int st, bool ng, double sc, EDefect def) {
    m_station = st; m_isNG = ng; m_score = sc; m_defect = def;
    Invalidate();
}
void CCameraView::Tick() { m_flash = !m_flash; if (m_isNG) Invalidate(); }

void CCameraView::SetImage(const std::vector<BYTE>& bytes) {
    ::EnterCriticalSection(&m_cs);
    if (bytes.empty()) { m_img.Destroy(); }
    else { CameraViewUtil::LoadImageFromBytes(bytes, m_img); }
    ::LeaveCriticalSection(&m_cs);
    Invalidate();
}

void CCameraView::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);
    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* pOld = mem.SelectObject(&bmp);
    ::EnterCriticalSection(&m_cs);
    DrawBg(mem, rc);
    ::LeaveCriticalSection(&m_cs);
    DrawBadge(mem, rc);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(pOld);
}

void CCameraView::DrawBg(CDC& dc, CRect& rc) {
    // 배경 — 항상 검은색
    dc.FillSolidRect(&rc, RGB(17,17,17));

    // 서버 수신 이미지가 있으면 전체 영역에 렌더링
    if (!m_img.IsNull()) {
        CRect inner(rc.left+2, rc.top+2, rc.right-2, rc.bottom-2);
        CameraViewUtil::DrawImageStretched(m_img, dc, inner);
    }

    // 외곽선만 그림 — 이미지 유무 무관
    CPen pen(PS_SOLID, 1, RGB(68,68,68)); CPen* p = dc.SelectObject(&pen);
    CBrush* pb = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);
    dc.SelectObject(p); dc.SelectObject(pb);
    // 이미지 없을 때 — 크로스헤어/레이블/내부박스 없이 검은 배경만 유지
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

CHeatmapView::CHeatmapView() : m_active(false) {
    ::InitializeCriticalSection(&m_cs);
}
void CHeatmapView::SetActive(bool a) { m_active=a; Invalidate(); }

void CHeatmapView::SetImage(const std::vector<BYTE>& bytes) {
    ::EnterCriticalSection(&m_cs);
    if (bytes.empty()) { m_img.Destroy(); }
    else { CameraViewUtil::LoadImageFromBytes(bytes, m_img); }
    ::LeaveCriticalSection(&m_cs);
    Invalidate();
}

void CHeatmapView::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);
    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* pOld = mem.SelectObject(&bmp);
    mem.FillSolidRect(&rc, RGB(17, 17, 17));
    ::EnterCriticalSection(&m_cs);
    if (!m_img.IsNull()) {
        CameraViewUtil::DrawImageStretched(m_img, mem, rc);
    }
    ::LeaveCriticalSection(&m_cs);
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
    , m_cx2(0.52), m_cy2(0.52) {
    ::InitializeCriticalSection(&m_cs);
}

void CPredMaskView::SetMask(bool is_active,
                             double cx1, double cy1,
                             double cx2, double cy2) {
    m_active = is_active;
    m_cx1 = cx1; m_cy1 = cy1;
    m_cx2 = cx2; m_cy2 = cy2;
    Invalidate();
}

void CPredMaskView::SetImage(const std::vector<BYTE>& bytes) {
    ::EnterCriticalSection(&m_cs);
    if (bytes.empty()) { m_img.Destroy(); }
    else { CameraViewUtil::LoadImageFromBytes(bytes, m_img); }
    ::LeaveCriticalSection(&m_cs);
    Invalidate();
}

void CPredMaskView::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);
    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* p_old = mem.SelectObject(&bmp);
    ::EnterCriticalSection(&m_cs);
    draw_bg(mem, rc);
    ::LeaveCriticalSection(&m_cs);
    // draw_mask_circles 제거 — 서버 수신 Pred Mask 이미지로 대체
    draw_label(mem, rc);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(p_old);
}

void CPredMaskView::draw_bg(CDC& dc, CRect& rc) {
    dc.FillSolidRect(&rc, RGB(17, 17, 17));
    // 수신한 Pred Mask PNG가 있으면 배경에 스트레치 렌더링
    if (!m_img.IsNull()) {
        CameraViewUtil::DrawImageStretched(m_img, dc, rc);
    }
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

// ── CNgHistoryList ───────────────────────────────────────────────────────────
IMPLEMENT_DYNAMIC(CNgHistoryList, CStatic)
BEGIN_MESSAGE_MAP(CNgHistoryList, CStatic)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

CNgHistoryList::CNgHistoryList() {
    ::InitializeCriticalSection(&m_cs);
}

void CNgHistoryList::PreSubclassWindow() {
    CStatic::PreSubclassWindow();
    // 리소스에서 WS_VSCROLL을 지정하지 않아도 런타임에 부여 — 스크롤바 강제 표시 가능.
    ModifyStyle(0, WS_VSCROLL);
    UpdateScrollInfo();
}

void CNgHistoryList::AddEntry(int id, int stationId, double score,
                              const CString& timeLabel,
                              const std::vector<BYTE>& img,
                              const std::vector<BYTE>& heat,
                              const std::vector<BYTE>& mask) {
    Entry e;
    e.id        = id;
    e.stationId = stationId;
    e.score     = score;
    e.time      = timeLabel;
    CameraViewUtil::LoadImageFromBytes(img,  e.img);
    CameraViewUtil::LoadImageFromBytes(heat, e.heat);
    CameraViewUtil::LoadImageFromBytes(mask, e.mask);

    ::EnterCriticalSection(&m_cs);
    // 동일 id가 이미 있으면 교체(중복 요청 대비). 없으면 맨 앞에 추가.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->id == id) {
            *it = std::move(e);
            ::LeaveCriticalSection(&m_cs);
            UpdateScrollInfo();
            Invalidate();
            return;
        }
    }
    m_entries.push_front(std::move(e));  // deque: push_front로 맨 앞 추가
    while (static_cast<int>(m_entries.size()) > m_maxEntries) {
        m_entries.pop_back();
    }
    ::LeaveCriticalSection(&m_cs);
    UpdateScrollInfo();
    Invalidate();
}

void CNgHistoryList::Clear() {
    ::EnterCriticalSection(&m_cs);
    m_entries.clear();
    m_scrollY = 0;
    ::LeaveCriticalSection(&m_cs);
    UpdateScrollInfo();
    Invalidate();
}

void CNgHistoryList::UpdateScrollInfo() {
    if (!GetSafeHwnd()) return;
    CRect rc; GetClientRect(&rc);
    int viewportH = rc.Height();
    int totalH    = TotalContentHeight();

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (totalH > 0) ? (totalH - 1) : 0;
    si.nPage  = (viewportH > 0) ? viewportH : 1;

    // 스크롤 범위가 줄어들었으면 현재 위치도 보정
    int maxPos = (totalH > viewportH) ? (totalH - viewportH) : 0;
    if (m_scrollY > maxPos) m_scrollY = maxPos;
    si.nPos = m_scrollY;

    SetScrollInfo(SB_VERT, &si, TRUE);
}

BOOL CNgHistoryList::OnEraseBkgnd(CDC* /*pDC*/) {
    // OnPaint에서 전체 배경을 채우므로 깜빡임 방지를 위해 기본 지움 생략
    return TRUE;
}

void CNgHistoryList::OnSize(UINT nType, int cx, int cy) {
    CStatic::OnSize(nType, cx, cy);
    UpdateScrollInfo();
    Invalidate();
}

void CNgHistoryList::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pSB) {
    CRect rc; GetClientRect(&rc);
    int viewportH = rc.Height();
    int maxPos    = (std::max)(0, TotalContentHeight() - viewportH);
    int delta     = 0;

    switch (nSBCode) {
        case SB_LINEUP:       delta = -m_rowH / 2; break;
        case SB_LINEDOWN:     delta =  m_rowH / 2; break;
        case SB_PAGEUP:       delta = -viewportH;  break;
        case SB_PAGEDOWN:     delta =  viewportH;  break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_TRACKPOS;
            GetScrollInfo(SB_VERT, &si);
            m_scrollY = si.nTrackPos;
            if (m_scrollY < 0)      m_scrollY = 0;
            if (m_scrollY > maxPos) m_scrollY = maxPos;
            SetScrollPos(SB_VERT, m_scrollY, TRUE);
            Invalidate();
            CStatic::OnVScroll(nSBCode, nPos, pSB);
            return;
        }
        default: CStatic::OnVScroll(nSBCode, nPos, pSB); return;
    }

    m_scrollY += delta;
    if (m_scrollY < 0)      m_scrollY = 0;
    if (m_scrollY > maxPos) m_scrollY = maxPos;
    SetScrollPos(SB_VERT, m_scrollY, TRUE);
    Invalidate();
    CStatic::OnVScroll(nSBCode, nPos, pSB);
}

BOOL CNgHistoryList::OnMouseWheel(UINT /*fFlags*/, short zDelta, CPoint /*pt*/) {
    CRect rc; GetClientRect(&rc);
    int viewportH = rc.Height();
    int maxPos    = (std::max)(0, TotalContentHeight() - viewportH);
    // 휠 한 노치(120) = 한 행 분량 스크롤
    int step = (zDelta / WHEEL_DELTA) * m_rowH;
    m_scrollY -= step;
    if (m_scrollY < 0)      m_scrollY = 0;
    if (m_scrollY > maxPos) m_scrollY = maxPos;
    SetScrollPos(SB_VERT, m_scrollY, TRUE);
    Invalidate();
    return TRUE;
}

void CNgHistoryList::DrawRow(CDC& dc, const Entry& e, const CRect& rowRc) {
    // 배경 — NG이면 약간 붉은 톤, 구분선
    dc.FillSolidRect(&rowRc, RGB(22, 22, 26));
    CPen sep(PS_SOLID, 1, RGB(45, 45, 50));
    CPen* p_old = dc.SelectObject(&sep);
    dc.MoveTo(rowRc.left,  rowRc.bottom - 1);
    dc.LineTo(rowRc.right, rowRc.bottom - 1);
    dc.SelectObject(p_old);

    // 레이아웃: [라벨 90px] [img] [heat] [mask], 패딩 4
    const int pad      = 4;
    const int labelW   = 90;
    const int thumbW   = (rowRc.Width() - labelW - pad * 5) / 3;
    const int thumbH   = rowRc.Height() - pad * 2;

    // 라벨 텍스트 ("#42" / "14:33:21" / "Score 0.87")
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(220, 220, 230));
    CFont f1; f1.CreatePointFont(75, _T("Tahoma"));
    CFont* p_f1 = dc.SelectObject(&f1);

    CString line1; line1.Format(_T("#%d"), e.id);
    CString line2 = e.time;
    CString line3; line3.Format(_T("Score %.2f"), e.score);

    CRect tr(rowRc.left + pad, rowRc.top + pad, rowRc.left + pad + labelW, rowRc.top + pad + 14);
    dc.DrawText(line1, &tr, DT_LEFT | DT_SINGLELINE);
    dc.SetTextColor(RGB(160, 180, 220));
    tr.OffsetRect(0, 14);
    dc.DrawText(line2, &tr, DT_LEFT | DT_SINGLELINE);
    dc.SetTextColor(RGB(255, 140, 140));
    tr.OffsetRect(0, 14);
    dc.DrawText(line3, &tr, DT_LEFT | DT_SINGLELINE);
    dc.SelectObject(p_f1);

    // 썸네일 3장
    auto drawThumb = [&](const CImage& img, int colIndex, LPCTSTR caption) {
        int x = rowRc.left + pad + labelW + pad + (thumbW + pad) * colIndex;
        int y = rowRc.top + pad;
        CRect tc(x, y, x + thumbW, y + thumbH);
        // 배경
        dc.FillSolidRect(&tc, RGB(10, 10, 14));
        if (!img.IsNull() && thumbW > 4 && thumbH > 4) {
            CImage& imgMut = const_cast<CImage&>(img);
            HDC hImgDC = imgMut.GetDC();
            int oldMode = ::SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
            ::SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
            ::StretchBlt(dc.GetSafeHdc(),
                         tc.left, tc.top, tc.Width(), tc.Height(),
                         hImgDC,
                         0, 0, img.GetWidth(), img.GetHeight(),
                         SRCCOPY);
            ::SetStretchBltMode(dc.GetSafeHdc(), oldMode);
            imgMut.ReleaseDC();
        }
        // 외곽선 + 캡션
        CPen pen(PS_SOLID, 1, RGB(68, 68, 78));
        CPen* p_o = dc.SelectObject(&pen);
        CBrush* p_ob = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
        dc.Rectangle(&tc);
        dc.SelectObject(p_o);
        dc.SelectObject(p_ob);
        // 좌상단 캡션
        dc.SetTextColor(RGB(170, 170, 180));
        CFont f2; f2.CreatePointFont(60, _T("Tahoma"));
        CFont* p_f2 = dc.SelectObject(&f2);
        CRect cr(tc.left + 2, tc.top + 1, tc.left + 90, tc.top + 13);
        dc.DrawText(caption, &cr, DT_LEFT | DT_SINGLELINE);
        dc.SelectObject(p_f2);
    };

    drawThumb(e.img,  0, _T("Image"));
    drawThumb(e.heat, 1, _T("Anomaly Map"));
    drawThumb(e.mask, 2, _T("Pred Mask"));
}

void CNgHistoryList::OnPaint() {
    CPaintDC dc(this);
    CRect rc; GetClientRect(&rc);

    // 더블 버퍼링
    CDC mem; CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* p_old = mem.SelectObject(&bmp);

    mem.FillSolidRect(&rc, RGB(17, 17, 20));

    ::EnterCriticalSection(&m_cs);
    if (m_entries.empty()) {
        mem.SetBkMode(TRANSPARENT);
        mem.SetTextColor(RGB(80, 80, 90));
        CFont f; f.CreatePointFont(80, _T("Tahoma"));
        CFont* p_f = mem.SelectObject(&f);
        mem.DrawText(_T("NG 이벤트 이력 없음 — 서버 수신 대기"),
                     &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        mem.SelectObject(p_f);
    } else {
        int firstIdx = m_scrollY / m_rowH;
        int offset   = m_scrollY % m_rowH;
        int y        = -offset;
        for (int i = firstIdx; i < static_cast<int>(m_entries.size()); ++i) {
            CRect rowRc(rc.left, y, rc.right, y + m_rowH);
            if (rowRc.bottom < rc.top) { y += m_rowH; continue; }
            if (rowRc.top > rc.bottom) break;
            DrawRow(mem, m_entries[i], rowRc);
            y += m_rowH;
        }
    }
    ::LeaveCriticalSection(&m_cs);

    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(p_old);
}
