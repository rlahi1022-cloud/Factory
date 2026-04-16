#pragma once
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include <vector>

class CPageHome : public CDialogEx {
    DECLARE_DYNAMIC(CPageHome)
public:
    CPageHome(CWnd* p = nullptr);
    enum { IDD = IDD_PAGE_HOME };
    void Update(const std::vector<InspectionRecord>& recs);
protected:
    CListCtrl m_listNG;
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};
