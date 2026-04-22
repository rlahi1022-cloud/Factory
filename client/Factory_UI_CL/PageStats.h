#pragma once
// ============================================================================
// PageStats.h — 통계/이력 페이지
// ============================================================================
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "NetworkClient.h"
#include "PacketBuilder.h"
#include <vector>
#include <string>

class CPageStats : public CDialogEx {
    DECLARE_DYNAMIC(CPageStats)
public:
    CPageStats(CWnd* p = nullptr);
    enum { IDD = IDD_PAGE_STATS };

    void Update(const std::vector<InspectionRecord>& recs);
    void OnInspectHistoryRes(const std::string& json);
    void OnStatsRes(const std::string& json);

    // 네트워크 클라이언트 설정 (MainTabDlg에서 호출)
    void SetNetworkClient(CNetworkClient* net) { m_net = net; }

    // 이력 항목의 이미지 3장을 서버에서 on-demand로 가져옴 (v0.10+)
    // 응답(프로토콜 117)은 NetworkClient가 WM_NET_NG_IMAGE로 라우팅 →
    // MainTabDlg가 station_id에 따라 PageStation1/2의 3개 뷰에 자동 표시.
    // 사용처: 메뉴/버튼에서 "이력 이미지 조회" 트리거 시 호출.
    void RequestInspectionImage(int inspectionId);

    // 최근 NG 이력 id 조회 (0이면 NG 없음) — 데모 트리거에 사용
    int GetLastNgInspectionId() const;

    // 특정 스테이션의 최신 NG 이력 id (0이면 없음)
    // 로그인 직후 각 스테이션 페이지의 3장 이미지를 자동 로드할 때 사용.
    int GetLastNgInspectionIdByStation(int station) const;

    // 특정 스테이션의 최신 NG들 inspection_id 목록 (최대 count개, 시간 역순).
    // NG가 부족하면 OK도 포함하여 채움. 로그인 직후 10건 이력 리스트 채우기용.
    std::vector<int> GetRecentInspectionIdsByStation(int station, int count) const;

    // inspection_id로 해당 레코드의 timestamp/score 조회.
    // 발견 시 true, 미발견 시 false (out 파라미터는 변경 안 됨).
    bool LookupInspectionMeta(int id, CString& outTime, double& outScore) const;

protected:
    CNetworkClient* m_net = nullptr;
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
