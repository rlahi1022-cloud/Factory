#pragma once
#include "pch.h"
#include "InspectionData.h"

// 카메라 뷰 (Pylon 이미지 플레이스홀더)
class CCameraView : public CStatic {
    DECLARE_DYNAMIC(CCameraView)
public:
    CCameraView();
    void SetInspection(int station, bool isNG, double score, EDefect defect);
    void Tick();  // NG 깜빡임 애니메이션
protected:
    int     m_station;
    bool    m_isNG;
    double  m_score;
    EDefect m_defect;
    bool    m_flash;
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
protected:
    bool m_active;
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
protected:
    bool   m_active;
    double m_cx1, m_cy1;  // 이상 영역 1 중심 (비율)
    double m_cx2, m_cy2;  // 이상 영역 2 중심 (비율)
    void draw_bg(CDC& dc, CRect& rc);
    void draw_mask_circles(CDC& dc, CRect& rc);
    void draw_label(CDC& dc, CRect& rc);
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};
