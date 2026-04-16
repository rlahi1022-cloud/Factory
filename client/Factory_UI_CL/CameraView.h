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
