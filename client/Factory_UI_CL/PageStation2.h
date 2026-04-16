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
