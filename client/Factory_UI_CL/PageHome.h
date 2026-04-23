#pragma once
#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "CameraView.h"   // CNgHistoryList
#include <functional>
#include <string>
#include <vector>

class CNetworkClient;  // fwd — 더블클릭 시 이미지 요청 송신용

class CPageHome : public CDialogEx {
    DECLARE_DYNAMIC(CPageHome)
public:
    CPageHome(CWnd* p = nullptr);
    enum { IDD = IDD_PAGE_HOME };

    // 상단 통계/대시보드 갱신 (실시간 recs 합계 기반)
    void Update(const std::vector<InspectionRecord>& recs);

    // 서버로부터 수신한 실시간 OK/NG 카운트를 스테이션별로 반영
    void UpdateStationCount(int stationId, int okCount, int ngCount);

    // v0.13.2: DB 기반 NG 이력 로드 (접속 직후 INSPECT_HISTORY_RES 수신 시 호출).
    // JSON 의 items 배열에서 NG 만 뽑아 m_listNG 를 채운다 (최대 MAX_NG_ROWS 건).
    void OnInspectHistoryRes(const std::string& json);

    // v0.13.2: 실시간 NG_PUSH 수신 시 리스트 맨 위에 1건 prepend.
    // 상한 MAX_NG_ROWS 초과 시 가장 오래된 항목 자동 제거.
    void AddNgRow(const InspectionRecord& r);

    // v0.14.7: 로그인 직후 STATS_RES(130) 받아서 Summary 초기 수치 세팅.
    //   JSON 파싱: total / ok / ng / s1_ok / s1_ng / s2_ok / s2_ng.
    //   이후엔 실시간 OK_COUNT_PUSH(112) + NG_PUSH(110) 로 증분 업데이트.
    void ApplyStatsRes(const std::string& json);

    // v0.14.6: 네트워크 핸들 + 탭 전환 콜백 주입.
    //   더블클릭 시 해당 행의 inspection_id 로 서버에 이미지 요청 + 해당 Station 탭으로 전환.
    void SetNetworkClient(CNetworkClient* net) { m_net = net; }
    void SetOnRequestShowImage(std::function<void(int station_id, int inspection_id)> cb) {
        m_onRequestShowImage = std::move(cb);
    }

protected:
    // v0.16.0: CListCtrl → CNgHistoryList 교체
    // 검정 배경 자체렌더링, 깜빡임 없음, Station1 NG 리스트와 동일 스타일
    CNgHistoryList m_listNG;
    // v0.16.0: CImageList 제거 (CNgHistoryList는 행 높이 자체 관리)

    // NG 리스트 상한 — 스크롤 가능하며 상한 초과 시 가장 오래된 것을 제거
    static constexpr int MAX_NG_ROWS = 200;

    // 리스트에 한 행을 맨 위에 삽입하는 내부 헬퍼
    void InsertNgItem(int row, const InspectionRecord& r);

    CNetworkClient* m_net = nullptr;
    std::function<void(int, int)> m_onRequestShowImage;  // (station_id, inspection_id)

    // v0.14.7: Summary 누적 카운터 (클라 시작 이후 계속 증가, 제한 없음).
    //   index 1 = Station1, index 2 = Station2. 0 은 사용 안 함.
    //   UpdateStationCount/ApplyStatsRes 가 서버 값으로 덮어쓰고(절대값),
    //   AddNgRow 는 NG 증분 +1 (다음 OK_COUNT_PUSH 까지 잠정치).
    int m_cumOk[3] = {0, 0, 0};
    int m_cumNg[3] = {0, 0, 0};

    // Summary 영역을 현재 누적 카운터로 다시 그림 (Total/OK/NG/DefectRate).
    void RefreshSummary();

    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);  // v0.16.0: 깜빡임 방지
    // v0.16.0: OnLvnDoubleClickNgList 제거 — CNgHistoryList 자체 클릭 처리로 전환
    DECLARE_MESSAGE_MAP()
};
