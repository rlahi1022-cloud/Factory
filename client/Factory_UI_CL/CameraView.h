#pragma once
#include "pch.h"
#include "InspectionData.h"
#include <atlimage.h>   // CImage — PNG/JPEG/BMP 디코드용 (ATL, MFC 프로젝트에 기본 포함)
#include <memory>       // std::unique_ptr — Entry 의 CImage 간접 소유
#include <vector>

// 카메라 뷰 (Pylon 이미지 플레이스홀더 + 서버 수신 이미지 렌더링)
class CCameraView : public CStatic {
    DECLARE_DYNAMIC(CCameraView)
public:
    CCameraView();
    void SetInspection(int station, bool isNG, double score, EDefect defect);
    void Tick();  // NG 깜빡임 애니메이션

    // SetImage: 서버에서 수신한 이미지 바이트(JPEG/PNG)를 디코드해서 화면에 표시
    // 빈 벡터를 넣으면 이미지 해제 (플레이스홀더 배경으로 복귀)
    void SetImage(const std::vector<BYTE>& bytes);
protected:
    int     m_station;
    bool    m_isNG;
    double  m_score;
    EDefect m_defect;
    bool    m_flash;
    CImage  m_img;  // 디코드된 이미지 (비었으면 IsNull() == true)
    void DrawBg(CDC& dc, CRect& rc);
    void DrawYolo(CDC& dc, CRect& rc);
    void DrawNgBox(CDC& dc, CRect& rc);
    void DrawBadge(CDC& dc, CRect& rc);
    void DrawScoreBar(CDC& dc, CRect& rc);
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

// 히트맵 뷰
class CHeatmapView : public CStatic {
    DECLARE_DYNAMIC(CHeatmapView)
public:
    CHeatmapView();
    void SetActive(bool active);
    // SetImage: 서버 수신 Anomaly Map PNG 바이너리 → 배경에 렌더링
    void SetImage(const std::vector<BYTE>& bytes);
protected:
    bool   m_active;
    CImage m_img;   // 디코드된 히트맵 (비었으면 플레이스홀더 배경)
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

// Pred Mask 뷰 — 원본 이미지 위에 이상 마스크 윤곽선(빨간 원)을 오버레이
class CPredMaskView : public CStatic {
    DECLARE_DYNAMIC(CPredMaskView)
public:
    CPredMaskView();
    // SetMask: 마스크 활성화 여부 및 이상 영역 위치(0.0~1.0 비율) 설정
    // is_active  — true 이면 마스크 원 표시
    // cx1, cy1   — 첫 번째 이상 영역 중심 (비율)
    // cx2, cy2   — 두 번째 이상 영역 중심 (비율, 0이면 미표시)
    void SetMask(bool is_active,
                 double cx1 = 0.55, double cy1 = 0.22,
                 double cx2 = 0.52, double cy2 = 0.52);
    // SetImage: 서버 수신 Pred Mask PNG 바이너리 → 배경에 렌더링
    void SetImage(const std::vector<BYTE>& bytes);
protected:
    bool   m_active;
    double m_cx1, m_cy1;  // 이상 영역 1 중심 (비율)
    double m_cx2, m_cy2;  // 이상 영역 2 중심 (비율)
    CImage m_img;         // 디코드된 마스크 이미지
    void draw_bg(CDC& dc, CRect& rc);
    void draw_mask_circles(CDC& dc, CRect& rc);
    void draw_label(CDC& dc, CRect& rc);
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

// ── 공통 헬퍼: 바이트 벡터 → CImage 디코드 ──────────────────────────────────
// 바이트를 HGLOBAL에 복사 → IStream 생성 → CImage::Load
// 실패 시 out 이미지는 Destroy()된 상태가 됨.
namespace CameraViewUtil {
    bool LoadImageFromBytes(const std::vector<BYTE>& bytes, CImage& out);
}

// NG 이벤트 이력 리스트 뷰 ───────────────────────────────────────────────────
// 최신 NG가 맨 위, 오래된 것이 아래쪽에 쌓이는 세로 스크롤 리스트.
// 각 행 = [라벨 영역][원본 썸네일][히트맵 썸네일][마스크 썸네일].
// 최대 m_maxEntries(기본 10)개만 유지하고, 초과 시 가장 오래된 항목을 버림.
// 마우스 휠 / 수직 스크롤바로 뷰포트 이동.
// 순수 UI — 네트워크 직접 호출 없음. PageStation1/2의 AddNgEntry를 통해 주입.
class CNgHistoryList : public CStatic {
    DECLARE_DYNAMIC(CNgHistoryList)
public:
    CNgHistoryList();

    // v0.14.2: 근본 해결 — CImage 를 값으로 보유하면 std::vector 의 재할당/이동 시
    // HBITMAP 의 얕은 복사가 발생해 ATLASSERT(hBitmap == m_hBitmap) 어설션을 유발한다.
    // std::unique_ptr 로 간접 보관하면 Entry 이동 비용이 포인터 교체로 축소되고
    // 소유권이 명확해진다. Entry 자체는 이동만 허용 (복사 금지).
    struct Entry {
        int     id        = 0;
        int     stationId = 0;
        double  score     = 0.0;
        CString time;                                // 표시용 문자열 ("HH:MM:SS" 또는 "#id")
        std::unique_ptr<CImage> img;                 // 디코드된 원본 (nullptr 허용 = 비었음)
        std::unique_ptr<CImage> heat;                // 디코드된 히트맵
        std::unique_ptr<CImage> mask;                // 디코드된 마스크

        Entry() = default;
        Entry(const Entry&) = delete;                // 복사 금지 — CImage 얕은 복사 차단
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&&) noexcept = default;           // 이동은 허용 (포인터 소유권 이전)
        Entry& operator=(Entry&&) noexcept = default;
    };

    // 새 NG 1건을 리스트 맨 위에 추가. 초과분은 꼬리부터 버림.
    // 빈 bytes는 해당 이미지 비움 처리(플레이스홀더).
    void AddEntry(int id, int stationId, double score,
                  const CString& timeLabel,
                  const std::vector<BYTE>& img,
                  const std::vector<BYTE>& heat,
                  const std::vector<BYTE>& mask);

    void Clear();
    int  Count() const { return static_cast<int>(m_entries.size()); }

protected:
    std::vector<Entry> m_entries;
    int m_maxEntries = 10;
    int m_rowH       = 58;           // 각 행 높이 (px)
    int m_scrollY    = 0;            // 현재 세로 스크롤 오프셋 (px)

    void UpdateScrollInfo();
    int  TotalContentHeight() const { return m_rowH * static_cast<int>(m_entries.size()); }
    void DrawRow(CDC& dc, const Entry& e, const CRect& rowRc);

    virtual void PreSubclassWindow() override;

    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pSB);
    afx_msg BOOL OnMouseWheel(UINT fFlags, short zDelta, CPoint pt);

    DECLARE_MESSAGE_MAP()
};
