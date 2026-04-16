#pragma once
// ============================================================================
// MainTabDlg.h — 메인 탭 다이얼로그 (애플리케이션의 중심 윈도우)
// ============================================================================
// 목적:
//   로그인 후 표시되는 메인 윈도우입니다.
//   5개 탭(종합현황/입고/조립/통계/모델관리)을 관리하고,
//   네트워크 클라이언트를 통해 메인서버와 실시간 통신합니다.
//
// 핵심 역할:
//   - CNetworkClient 소유 및 관리 (서버 접속/해제)
//   - 서버 푸시 메시지 수신 → 각 페이지에 데이터 전달
//   - 시뮬레이션 데이터와 실제 서버 데이터 병행 지원
//   - 서버 헬스 상태 LED 표시
//   - 타이틀바, 툴바, 상태바 커스텀 그리기
// ============================================================================

#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "NetworkClient.h"   // TCP 네트워크 클라이언트
#include "PacketBuilder.h"   // 패킷 빌더
#include "PageHome.h"
#include "PageStation1.h"
#include "PageStation2.h"
#include "PageStats.h"
#include "PageModel.h"
#include <vector>
#include <memory>

class CMainTabDlg : public CDialogEx {
    DECLARE_DYNAMIC(CMainTabDlg)
public:
    // 생성자: 로그인 세션 정보를 받아서 초기화
    CMainTabDlg(const UserSession& session, CWnd* p = nullptr);
    virtual ~CMainTabDlg();
    enum { IDD = IDD_MAIN_DLG };

    // GetNetworkClient: 다른 페이지에서 네트워크 클라이언트에 접근할 때 사용
    // 예) 모델 페이지에서 재학습 요청을 전송할 때
    CNetworkClient& GetNetworkClient() { return m_net; }

protected:
    // ── 사용자 세션 ──────────────────────────────────────────────────────
    UserSession m_session;  // 로그인 정보 (이름, 역할, 사원ID)

    // ── 네트워크 클라이언트 ──────────────────────────────────────────────
    CNetworkClient m_net;   // 메인서버와의 TCP 통신 담당
    bool m_bConnected;      // 서버 연결 상태

    // ── 탭 컨트롤 ────────────────────────────────────────────────────────
    CTabCtrl m_tab;         // 탭 UI 컨트롤
    int m_activeTab;        // 현재 활성 탭 인덱스 (0~4)

    // ── 페이지 (각 탭의 내용 다이얼로그) ─────────────────────────────────
    std::unique_ptr<CPageHome>     m_home;   // 탭 0: 종합 현황
    std::unique_ptr<CPageStation1> m_st1;    // 탭 1: ① 입고 검사
    std::unique_ptr<CPageStation2> m_st2;    // 탭 2: ② 조립 검사
    std::unique_ptr<CPageStats>    m_stats;  // 탭 3: 통계/이력
    std::unique_ptr<CPageModel>    m_model;  // 탭 4: 모델 관리

    // ── 검사 데이터 ──────────────────────────────────────────────────────
    std::vector<InspectionRecord> m_recs;  // 검사 이력 (시뮬레이션 + 서버 데이터)
    int m_nextId;   // 다음 검사 레코드 ID
    int m_tick;     // 타이머 틱 카운터

    // ── 서버 상태 ────────────────────────────────────────────────────────
    bool m_sv0;     // 학습 PC 상태 (true=정상, false=장애)
    bool m_sv1;     // 추론 PC #1 상태
    bool m_sv2;     // 추론 PC #2 상태

    // ── 폰트 ─────────────────────────────────────────────────────────────
    CFont m_fNormal;  // 일반 글꼴 (9pt Tahoma)
    CFont m_fBold;    // 굵은 글꼴
    CFont m_fSmall;   // 작은 글꼴 (7.5pt)

    // ── 페이지 관리 함수 ─────────────────────────────────────────────────
    void CreatePages();           // 5개 페이지 다이얼로그 생성
    void SwitchTab(int idx);      // 탭 전환 (표시/숨김 처리)
    void LayoutPages();           // 페이지를 컨텐츠 영역에 맞춤
    void PushUpdate();            // 모든 페이지에 최신 데이터 전달

    // ── 네트워크 관리 ────────────────────────────────────────────────────
    void ConnectToServer();       // 서버 접속 시도 + 접속 직후 LOGIN_REQ 전송

    // ── 레이아웃 영역 계산 ───────────────────────────────────────────────
    CRect ContentRect();  // 탭 컨텐츠 영역 (탭바 아래, 상태바 위)
    CRect TitleRect();    // 타이틀바 영역 (최상단 22px)
    CRect ToolbarRect();  // 툴바 영역 (타이틀바 아래 24px)
    CRect StatusRect();   // 상태바 영역 (최하단 18px)

    // ── 커스텀 그리기 ────────────────────────────────────────────────────
    void DrawTitle(CDC& dc);    // 그래디언트 타이틀바 그리기
    void DrawToolbar(CDC& dc);  // 툴바 + 서버 LED 그리기
    void DrawStatus(CDC& dc);   // 상태바 (TCP, DB, 마지막 검사 시각)
    void DrawLed(CDC& dc, int x, int y, bool ok, LPCTSTR lbl);  // LED 인디케이터

    // ── MFC 오버라이드 ───────────────────────────────────────────────────
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual void OnOK() override {}          // Enter 키로 닫히는 것 방지
    virtual void OnCancel() override;        // X 버튼 / ESC 처리

    // ── Windows 메시지 핸들러 ────────────────────────────────────────────
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR id);
    afx_msg void OnTabChanged(NMHDR* pNMHDR, LRESULT* pResult);

    // ── 메뉴 커맨드 핸들러 ───────────────────────────────────────────────
    afx_msg void OnFileExit();
    afx_msg void OnViewRefresh();
    afx_msg void OnInspectStart();
    afx_msg void OnInspectStop();
    afx_msg void OnHelpAbout();
    afx_msg void OnNetConnect();       // 서버 수동 접속
    afx_msg void OnNetDisconnect();    // 서버 수동 해제
    afx_msg void OnLogout();           // 로그아웃 → 로그인 화면으로 복귀

    // ── 네트워크 메시지 핸들러 (서버로부터 수신) ─────────────────────────
    // 이 함수들은 CNetworkClient의 수신 스레드가 PostMessage로 보낸
    // 커스텀 메시지를 처리합니다.

    // OnNetConnected: 서버 접속 성공 시 호출 (WM_NET_CONNECTED)
    afx_msg LRESULT OnNetConnected(WPARAM wParam, LPARAM lParam);
    // OnNetDisconnected: 서버 연결 끊김 시 호출 (WM_NET_DISCONNECTED)
    afx_msg LRESULT OnNetDisconnectedMsg(WPARAM wParam, LPARAM lParam);
    // OnNetNgPush: NG 검사 결과 푸시 수신 (WM_NET_NG_PUSH, 프로토콜 110)
    afx_msg LRESULT OnNetNgPush(WPARAM wParam, LPARAM lParam);
    // OnNetOkCountPush: OK/NG 카운트 푸시 수신 (WM_NET_OK_COUNT_PUSH, 프로토콜 112)
    afx_msg LRESULT OnNetOkCountPush(WPARAM wParam, LPARAM lParam);
    // OnNetHealthPush: 서버 헬스 상태 푸시 수신 (WM_NET_HEALTH_PUSH, 프로토콜 170)
    afx_msg LRESULT OnNetHealthPush(WPARAM wParam, LPARAM lParam);
    // OnNetResponse: 범용 응답 수신 (WM_NET_RESPONSE)
    afx_msg LRESULT OnNetResponse(WPARAM wParam, LPARAM lParam);
    // OnNetRetrainProgress: 재학습 진행률 수신 (WM_NET_RETRAIN_PROGRESS, 프로토콜 154)
    afx_msg LRESULT OnNetRetrainProgress(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()
};
