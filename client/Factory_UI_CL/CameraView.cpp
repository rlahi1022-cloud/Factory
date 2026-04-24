#include "pch.h"
#include "CameraView.h"

// ── 공통 이미지 디코더 ─────────────────────────────────────────────────────
// CImage::Load는 IStream 기반이므로, 바이트 벡터를 HGLOBAL에 복사 후 스트림 생성.
// 실패 시 out.Destroy()로 초기화된 상태를 보장.
namespace CameraViewUtil {

bool LoadImageFromBytes(const std::vector<BYTE>& bytes, CImage& out) {
    // Destroy 전에 DC가 열려있으면 안 되므로 IsNull 체크 후 안전하게 파괴.
    // ReleaseDC()는 DC가 열린 상태에서만 호출해야 하므로 여기서는 호출하지 않음
    //   → DC 열린 채 Destroy가 발생하던 근본 원인은 Paint 중 SetImage 호출이므로
    //     CRITICAL_SECTION으로 외부 차단함.
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
// v0.14.x: CImage 내부 DC 캐시를 사용하지 않고 별도 DC 생성 — DC 캐시 충돌 원천 방지
static void DrawImageStretched(CImage& img, CDC& dc, const CRect& rc) {
    if (img.IsNull() || rc.Width() <= 0 || rc.Height() <= 0) return;
    HDC hSrcDC = ::CreateCompatibleDC(dc.GetSafeHdc());
    HBITMAP hOld = (HBITMAP)::SelectObject(hSrcDC, (HBITMAP)img);
    int oldMode = ::SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
    ::SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
    ::StretchBlt(dc.GetSafeHdc(),
                 rc.left, rc.top, rc.Width(), rc.Height(),
                 hSrcDC,
                 0, 0, img.GetWidth(), img.GetHeight(),
                 SRCCOPY);
    ::SetStretchBltMode(dc.GetSafeHdc(), oldMode);
    ::SelectObject(hSrcDC, hOld);
    ::DeleteDC(hSrcDC);
}

} // namespace CameraViewUtil

// ── CCameraView ────────────────────────────────────────────────────────────
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
    // v0.14.7: 하단 NG/OK 큰 배지(빨간/초록 띠) 제거 — 사용자 요청.
    //   결과는 상단 "Result" 영역 텍스트 + 상단 3뷰에서 충분히 표현됨.
    // DrawBadge(mem, rc);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(pOld);
}

void CCameraView::DrawBg(CDC& dc, CRect& rc) {
    // 배경 — 항상 검정색
    dc.FillSolidRect(&rc, RGB(17,17,17));

    // 서버 원시 이미지가 있으면 전체 영역에 렌더링
    if (!m_img.IsNull()) {
        CRect inner(rc.left+2, rc.top+2, rc.right-2, rc.bottom-2);
        CameraViewUtil::DrawImageStretched(m_img, dc, inner);
    }

    // 외곽선만 그림 — 이미지 유무 무관
    CPen pen(PS_SOLID, 1, RGB(68,68,68)); CPen* p = dc.SelectObject(&pen);
    CBrush* pb = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);
    dc.SelectObject(p); dc.SelectObject(pb);
    // 이미지 있을 때 → 크로스헤어/레이블/내부박스 없이 검은 배경만 유지
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

// ── CHeatmapView ───────────────────────────────────────────────────────────
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

// ── CPredMaskView ──────────────────────────────────────────────────────────
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
    draw_label(mem, rc);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(p_old);
}

void CPredMaskView::draw_bg(CDC& dc, CRect& rc) {
    dc.FillSolidRect(&rc, RGB(17, 17, 17));
    // 원시 Pred Mask PNG가 있으면 배경에 스트레치로 렌더링
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

// ── CNgHistoryList ─────────────────────────────────────────────────────────
// v0.14.5: 종합현황(PageHome) 리스트와 동일 컨셉 — 텍스트 컬럼만 표시.
//   [ID | 스테이션 | 시각 | 결과 | 점수] 헤더 + 스크롤 가능한 데이터 행.
//   썸네일 제거로 CImage 얕은 복사/DC 충돌 이슈 원천 소멸.
//   실이미지는 상단 3뷰에서 확인 — 이력 리스트는 메타정보만 추적.
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
                              const std::vector<BYTE>& /*img*/,
                              const std::vector<BYTE>& /*heat*/,
                              const std::vector<BYTE>& /*mask*/) {
    // v0.14.5: 이미지 bytes 는 더이상 이 리스트에서 사용하지 않음.
    //   상단 3뷰(CCameraView/CHeatmapView/CPredMaskView) 가 책임짐.
    //   파라미터는 유지 — PageStation1/2 호출부 API 호환.
    Entry e;
    e.id        = id;
    e.stationId = stationId;
    e.score     = score;
    e.time      = timeLabel;

    ::EnterCriticalSection(&m_cs);
    // 동일 id가 이미 있으면 교체(중복 요청 대비). 없으면 맨 앞에 추가.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->id == id) {
            *it = e;
            ::LeaveCriticalSection(&m_cs);
            UpdateScrollInfo();
            Invalidate();
            return;
        }
    }
    m_entries.push_front(e);  // deque: push_front 로 맨 앞 추가
    while (static_cast<int>(m_entries.size()) > m_maxEntries) {
        m_entries.pop_back();
    }
    ::LeaveCriticalSection(&m_cs);
    UpdateScrollInfo();
    Invalidate();
}

// v0.16.0: Entry 직접 주입 오버로드 — Station2 YOLO 디텍션 포함
void CNgHistoryList::AddEntry(int /*id*/, int /*stationId*/, double /*score*/,
                              const CString& /*timeLabel*/,
                              const std::vector<BYTE>& /*img*/,
                              const std::vector<BYTE>& /*heat*/,
                              const std::vector<BYTE>& /*mask*/,
                              const Entry& entryOverride) {
    ::EnterCriticalSection(&m_cs);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->id == entryOverride.id) {
            *it = entryOverride;
            ::LeaveCriticalSection(&m_cs);
            UpdateScrollInfo();
            Invalidate();
            return;
        }
    }
    m_entries.push_front(entryOverride);
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
    // v0.14.5: 헤더 영역 제외한 실제 행 표시 영역
    int viewportH = (std::max)(0, rc.Height() - m_headerH);
    int totalH    = TotalContentHeight();

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (totalH > 0) ? (totalH - 1) : 0;
    si.nPage  = (viewportH > 0) ? viewportH : 1;

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
    int viewportH = (std::max)(0, rc.Height() - m_headerH);
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
    int viewportH = (std::max)(0, rc.Height() - m_headerH);
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

// v0.14.5: 컬럼 폭/오프셋 상수 — 헤더와 데이터 행에서 공용
// v0.16.0b: 876px 너비 꽉 채우도록 확대
// 합계: 8 + 80+80+110+60+70+100+80+60 = 648 → 여백 포함 876px 활용
namespace {
    constexpr int kPad       = 8;
    constexpr int kColIdW    = 80;
    constexpr int kColStaW   = 80;
    constexpr int kColTimeW  = 110;
    constexpr int kColResW   = 60;
    constexpr int kColScoreW = 70;
    constexpr int kColDetClsW  = 100;  // v0.16.0: YOLO 클래스
    constexpr int kColDetConfW = 80;   // v0.16.0: YOLO 신뢰도
    constexpr int kColDetOkW   = 60;   // v0.16.0: YOLO 판정
}

void CNgHistoryList::DrawHeader(CDC& dc, const CRect& rc) {
    dc.FillSolidRect(&rc, RGB(36, 36, 44));
    CPen sep(PS_SOLID, 1, RGB(70, 70, 80));
    CPen* p_old = dc.SelectObject(&sep);
    dc.MoveTo(rc.left,  rc.bottom - 1);
    dc.LineTo(rc.right, rc.bottom - 1);
    dc.SelectObject(p_old);

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(210, 210, 220));
    CFont f; f.CreatePointFont(110, _T("Tahoma")); // v0.16.0b: 90 → 110 (글자 큰 화면)
    CFont* p_f = dc.SelectObject(&f);

    int x = rc.left + kPad;
    auto cell = [&](LPCTSTR text, int w) {
        CRect cr(x, rc.top, x + w, rc.bottom);
        dc.DrawText(text, &cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += w;
    };
    cell(_T("ID"),       kColIdW);
    cell(_T("스테이션"), kColStaW);
    cell(_T("시각"),     kColTimeW);
    cell(_T("결과"),     kColResW);
    cell(_T("점수"),     kColScoreW);
    // v0.16.0: Station2 YOLO 디텍션 컬럼
    // v0.15.7: Station1 은 PatchCore 이상탐지 모델이라 클래스/신뢰도 개념 없음 → 숨김
    if (m_showYoloCols) {
        cell(_T("클래스"),   kColDetClsW);
        cell(_T("신뢰도"),   kColDetConfW);
        cell(_T("판정"),     kColDetOkW);
    }
    dc.SelectObject(p_f);
}

void CNgHistoryList::DrawRow(CDC& dc, const Entry& e, const CRect& rowRc) {
    dc.FillSolidRect(&rowRc, RGB(22, 22, 26));
    CPen sep(PS_SOLID, 1, RGB(40, 40, 46));
    CPen* p_old = dc.SelectObject(&sep);
    dc.MoveTo(rowRc.left,  rowRc.bottom - 1);
    dc.LineTo(rowRc.right, rowRc.bottom - 1);
    dc.SelectObject(p_old);

    dc.SetBkMode(TRANSPARENT);
    CFont f; f.CreatePointFont(110, _T("Tahoma")); // v0.16.0b: 90 → 110 (글자 큰 화면)
    CFont* p_f = dc.SelectObject(&f);

    CString s;
    int x = rowRc.left + kPad;
    auto cell = [&](COLORREF color, LPCTSTR text, int w) {
        dc.SetTextColor(color);
        CRect cr(x, rowRc.top, x + w, rowRc.bottom);
        dc.DrawText(text, &cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += w;
    };

    s.Format(_T("%d"), e.id);           cell(RGB(220, 220, 230), s,            kColIdW);
    s.Format(_T("#%d"), e.stationId);   cell(RGB(180, 200, 235), s,            kColStaW);
                                        cell(RGB(170, 190, 220), e.time,       kColTimeW);
                                        cell(RGB(255, 130, 130), _T("NG"),     kColResW);
    s.Format(_T("%.2f"), e.score);      cell(RGB(240, 220, 160), s,            kColScoreW);
    // v0.16.0: YOLO 디텍션 컬럼 — Station1은 비어있으면 "-" 표시
    // v0.15.7: m_showYoloCols=false (Station1) 이면 YOLO 3컬럼 전부 생략
    if (m_showYoloCols) {
        if (!e.detClass.IsEmpty()) {
                                            cell(RGB(160, 220, 180), e.detClass,   kColDetClsW);
            s.Format(_T("%.2f"), e.detConf);cell(RGB(160, 220, 180), s,            kColDetConfW);
                                            cell(e.detOk ? RGB(100,200,100) : RGB(255,100,100),
                                                 e.detOk ? _T("OK") : _T("NG"),   kColDetOkW);
        } else {
                                            cell(RGB(100, 100, 110), _T("-"),      kColDetClsW);
                                            cell(RGB(100, 100, 110), _T("-"),      kColDetConfW);
                                            cell(RGB(100, 100, 110), _T("-"),      kColDetOkW);
        }
    }

    dc.SelectObject(p_f);
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

    // (1) 고정 헤더 — 스크롤 영향 없음
    CRect headerRc(rc.left, rc.top, rc.right, rc.top + m_headerH);
    DrawHeader(mem, headerRc);

    // (2) 데이터 영역 — 헤더 아래
    CRect body(rc.left, rc.top + m_headerH, rc.right, rc.bottom);

    ::EnterCriticalSection(&m_cs);
    if (m_entries.empty()) {
        mem.SetBkMode(TRANSPARENT);
        mem.SetTextColor(RGB(80, 80, 90));
        CFont f; f.CreatePointFont(80, _T("Tahoma"));
        CFont* p_f = mem.SelectObject(&f);
        mem.DrawText(_T("NG 이벤트 이력 없음 — 서버 수신 대기"),
                     &body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        mem.SelectObject(p_f);
    } else {
        int firstIdx = (m_rowH > 0) ? (m_scrollY / m_rowH) : 0;
        int offset   = (m_rowH > 0) ? (m_scrollY % m_rowH) : 0;
        int y        = body.top - offset;
        // 클리핑 — 데이터 영역 밖은 그리지 않도록 절단
        mem.SaveDC();
        mem.IntersectClipRect(&body);
        for (int i = firstIdx; i < static_cast<int>(m_entries.size()); ++i) {
            CRect rowRc(body.left, y, body.right, y + m_rowH);
            if (rowRc.bottom < body.top) { y += m_rowH; continue; }
            if (rowRc.top > body.bottom) break;
            DrawRow(mem, m_entries[i], rowRc);
            y += m_rowH;
        }
        mem.RestoreDC(-1);
    }
    ::LeaveCriticalSection(&m_cs);

    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(p_old);
}
