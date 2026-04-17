#pragma once
// ============================================================================
// PageStats.h — 통계/이력 페이지
// ============================================================================
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include <vector>
#include <string>

class CPageStats : public CDialogEx {
    DECLARE_DYNAMIC(CPageStats)
public:
    CPageStats(CWnd* p = nullptr);
    enum { IDD = IDD_PAGE_STATS };

    void Update(const std::vector<InspectionRecord>& recs);
    // 서버 응답 수신 핸들러 (MainTabDlg에서 호출)
    void OnInspectHistoryRes(const std::string& json);  // 프로토콜 115
    void OnStatsRes(const std::string& json);           // 프로토콜 131

protected:
    std::vector<InspectionRecord> m_recs;

    struct TPoint { CString lbl; double s1, s2; int lat; };
    struct PItem  { CString name; int cnt; };
    std::vector<TPoint> m_trend;
    std::vector<PItem>  m_pareto;

    void Rebuild();
    void DrawTrend(CDC& dc, CRect rc);
    void DrawPareto(CDC& dc, CRect rc);
    void DrawLatency(CDC& dc, CRect rc);
    void DrawGrid(CDC& dc, CRect rc, int rows, int cols);

    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    afx_msg void OnPaint();
    afx_msg void OnBtnQuery();
    afx_msg void OnBtnExportCSV();

    DECLARE_MESSAGE_MAP()
};
