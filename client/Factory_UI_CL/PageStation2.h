#pragma once
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "CameraView.h"
#include <vector>

class CPageStation2 : public CDialogEx {
    DECLARE_DYNAMIC(CPageStation2)
public:
    CPageStation2(CWnd* p=nullptr);
    enum { IDD = IDD_PAGE_STATION2 };
    void Update(const std::vector<InspectionRecord>& recs);
    void Tick();
    // SetImages: 서버 NG_PUSH 바이너리 주입 (원본/히트맵만 사용, pred_mask는 Station2 미해당)
    void SetImages(const std::vector<BYTE>& image,
                   const std::vector<BYTE>& heatmap,
                   const std::vector<BYTE>& pred_mask);
protected:
    CCameraView  m_cam;
    CHeatmapView m_heat;
    CListCtrl    m_listYolo;
    InspectionRecord m_last;
    void Refresh();
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnBtnDefect();
    afx_msg void OnBtnRework();
    DECLARE_MESSAGE_MAP()
};
