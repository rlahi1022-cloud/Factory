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
    // 서버로부터 수신한 실시간 OK/NG 카운트를 스테이션별로 반영
    void UpdateStationCount(int stationId, int okCount, int ngCount);
protected:
    CListCtrl m_listNG;
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};
