#pragma once
// ============================================================================
// LoginDlg.h — 로그인/회원가입 다이얼로그
// ============================================================================
// 목적:
//   프로그램 시작 시 사용자 인증을 수행합니다.
//   서버(MainServer)에 LOGIN_REQ(100) 또는 REGISTER_REQ(104)를 전송하여
//   DB 기반 인증/가입을 처리합니다.
// ============================================================================

#include "pch.h"
#include "Resource.h"
#include "InspectionData.h"
#include "NetworkClient.h"

class CLoginDlg : public CDialogEx {
    DECLARE_DYNAMIC(CLoginDlg)
public:
    CLoginDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_LOGIN_DLG };

    // m_session: 로그인 성공 시 세션 정보 (MainTabDlg에서 참조)
    UserSession m_session;

protected:
    bool m_regMode;              // 모드 플래그: true=회원가입, false=로그인
    bool m_waitingResponse;      // 서버 응답 대기 중 (중복 클릭 방지)

    // ── 네트워크 ──
    CNetworkClient m_loginNet;   // 로그인/가입 전용 임시 네트워크 클라이언트

    // ── 컨트롤 변수 ──
    CEdit     m_edUser;          // 사용자 이름 입력
    CEdit     m_edPass;          // 비밀번호 입력
    CEdit     m_edEmpId;         // 사원 ID (회원가입 전용)
    CEdit     m_edPassConfirm;   // 비밀번호 확인 (회원가입 전용)
    CComboBox m_cbRole;          // 권한 등급 (회원가입 전용)
    CStatic   m_stErr;           // 에러 메시지 표시

    // SwitchMode: 로그인 ↔ 회원가입 모드 전환
    void SwitchMode(bool reg);

    // SetError: 에러 메시지를 화면에 표시
    void SetError(LPCTSTR msg);

    // ── MFC 오버라이드 ──
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    // ── 버튼 핸들러 ──
    afx_msg void OnBtnOK();
    afx_msg void OnBtnCancel();
    afx_msg void OnBtnSwitch();
    afx_msg void OnPaint();

    // ── 네트워크 응답 핸들러 ──
    afx_msg LRESULT OnLoginRes(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnRegisterRes(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnNetDisconnected(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()
};
