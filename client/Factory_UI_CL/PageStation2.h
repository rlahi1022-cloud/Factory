#pragma once
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "CameraView.h"
#include <vector>

class CNetworkClient;   // fwd — pause/resume 송신용 (v0.14.3)

class CPageStation2 : public CDialogEx {
    DECLARE_DYNAMIC(CPageStation2)
public:
    CPageStation2(CWnd* p=nullptr);
    enum { IDD = IDD_PAGE_STATION2 };
    void Update(const std::vector<InspectionRecord>& recs);
    void Tick();
    // SetImages: 서버 NG_PUSH 바이너리 주입 (원본/히트맵/pred_mask 모두 사용)
    void SetImages(const std::vector<BYTE>& image,
                   const std::vector<BYTE>& heatmap,
                   const std::vector<BYTE>& pred_mask);

    // v0.15.0: NG 이력 리스트 누적
    void AddNgEntry(int id, double score, const CString& timeLabel,
                    const std::vector<BYTE>& image,
                    const std::vector<BYTE>& heatmap,
                    const std::vector<BYTE>& pred_mask);

    // v0.14.3: 부모가 네트워크 핸들 주입 — Start/Stop 버튼용
    void SetNetworkClient(CNetworkClient* net) { m_net = net; }
protected:
    CCameraView     m_cam;
    CHeatmapView    m_heat;
    CPredMaskView   m_mask;      // v0.15.0: Pred Mask 뷰 (검정 배경 통일)
    CListCtrl       m_listYolo;
    CNgHistoryList  m_ngList;    // v0.15.0: NG 이력 리스트
    InspectionRecord m_last;
    CNetworkClient* m_net = nullptr;   // v0.14.3
    void Refresh();
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnBtnDefect();
    afx_msg void OnBtnRework();
    // v0.14.3: 2공정 검사 시작/중지 (station_filter=2)
    afx_msg void OnBtnS2Start();
    afx_msg void OnBtnS2Stop();
    DECLARE_MESSAGE_MAP()
};
