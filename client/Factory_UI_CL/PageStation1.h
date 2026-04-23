#pragma once
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "CameraView.h"
#include <vector>

class CNetworkClient;   // fwd — pause/resume 송신용

class CPageStation1 : public CDialogEx {
    DECLARE_DYNAMIC(CPageStation1)
public:
    CPageStation1(CWnd* p=nullptr);

    // v0.14.3: 부모가 네트워크 핸들 주입 — Start/Stop 버튼이 INSPECT_CONTROL_REQ 송신
    void SetNetworkClient(CNetworkClient* net) { m_net = net; }
    enum { IDD = IDD_PAGE_STATION1 };
    void Update(const std::vector<InspectionRecord>& recs);
    void Tick();
    // SetImages: 서버 NG_PUSH 바이너리 블록을 3개 뷰에 주입 (원본/히트맵/마스크)
    // 빈 벡터를 넣으면 해당 뷰는 플레이스홀더 배경으로 복귀
    void SetImages(const std::vector<BYTE>& image,
                   const std::vector<BYTE>& heatmap,
                   const std::vector<BYTE>& pred_mask);

    // AddNgEntry: NG 이벤트를 하단 이력 리스트에 누적 (최대 10건, 초과시 가장 오래된 것 제거)
    // 상단 대형 3뷰의 SetImages와 독립적으로 호출 가능 — 최신 1건은 상단, 10건은 하단.
    void AddNgEntry(int id, double score, const CString& timeLabel,
                    const std::vector<BYTE>& image,
                    const std::vector<BYTE>& heatmap,
                    const std::vector<BYTE>& pred_mask);

    // v0.14.6: 로그인 직후 DB 이력 응답(INSPECT_HISTORY_RES) 에서 station_id=1 NG 만
    //   골라 하단 m_ngList 를 초기 채움. 텍스트 전용 리스트이므로 이미지는 주입하지 않음.
    //   실시간 NG_PUSH 가 들어오면 기존 AddNgEntry 경로로 맨 위에 누적됨.
    void PopulateNgHistoryFromJson(const std::string& json);

    // v0.16.0: COM 포트 동적 탐색 — 레지스트리에서 실제 포트 읽어 레이블 갱신
    // labelId 기본값 = IDC_STATIC_S1_SERIAL (Station1용)
    void UpdateSerialPortLabel(int labelId = IDC_STATIC_S1_SERIAL);

    // v0.15.0: MODEL_LIST_RES(151) 수신 시 검사 설정 라벨 갱신
    // items[] 중 station_id=1 & is_active=1 인 항목의 model_type/version/accuracy 를 표시.
    void UpdateModelInfo(const std::string& json);
protected:
    CCameraView     m_cam;     // 패널 1: 원본 이미지 (Image)
    CHeatmapView    m_heat;    // 패널 2: Anomaly Map 오버레이
    CPredMaskView   m_mask;    // 패널 3: Pred Mask 오버레이
    CNgHistoryList  m_ngList;  // 하단: 최근 NG 10건 이력 리스트
    InspectionRecord m_last;
    CNetworkClient*  m_net = nullptr;   // v0.14.3: Start/Stop 버튼용 네트워크 핸들
    void Refresh();
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    // v0.14.7: Manual OK/NG/Arduino 버튼 제거.
    // v0.14.3: 1공정 검사 시작/중지 (station_filter=1)
    afx_msg void OnBtnS1Start();
    afx_msg void OnBtnS1Stop();
    DECLARE_MESSAGE_MAP()
};
