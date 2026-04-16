#pragma once
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "CameraView.h"
#include <vector>

class CPageStation1 : public CDialogEx {
    DECLARE_DYNAMIC(CPageStation1)
public:
    CPageStation1(CWnd* p=nullptr);
    enum { IDD = IDD_PAGE_STATION1 };
    void Update(const std::vector<InspectionRecord>& recs);
    void Tick();
protected:
    CCameraView  m_cam;
    CHeatmapView m_heat;
    InspectionRecord m_last;
    void Refresh();
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnBtnOK();
    afx_msg void OnBtnNG();
    afx_msg void OnBtnArduino();
    DECLARE_MESSAGE_MAP()
};
