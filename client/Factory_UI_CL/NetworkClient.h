#pragma once
// ============================================================================
// NetworkClient.h
// ============================================================================
// 목적:
//   메인서버(포트 9010)에 TCP 접속하여 데이터를 주고받는 네트워크 클라이언트입니다.
//   MFC GUI는 "UI 스레드"에서 화면을 그리므로, 네트워크 수신은 반드시
//   별도의 "백그라운드 스레드"에서 수행해야 합니다.
//   (UI 스레드에서 recv()를 호출하면 데이터가 올 때까지 화면이 멈춤!)
//
// 핵심 설계:
//   ┌──────────────┐    PostMessage     ┌──────────────┐
//   │ 수신 스레드    │  ──────────────→  │  UI 스레드    │
//   │ (RecvLoop)   │   WM_NET_xxx     │ (MainTabDlg) │
//   └──────────────┘                    └──────────────┘
//        ↑ recv()                         ↓ Send()
//   ┌──────────────┐                    ┌──────────────┐
//   │  메인 서버     │  ←────────────── │  UI에서 요청  │
//   │  (포트 9010)  │    send()         │  (로그인 등)  │
//   └──────────────┘                    └──────────────┘
//
//   - 수신 스레드가 패킷을 받으면 PostMessage로 UI 스레드에 알림
//   - UI 스레드는 메시지 핸들러에서 안전하게 화면 업데이트
//   - 전송(Send)은 어느 스레드에서든 호출 가능 (CriticalSection으로 보호)
//
// 사용법:
//   CNetworkClient net;
//   net.Connect(_T("10.10.10.130"), 9010, m_hWnd);  // 서버 접속
//   net.SendJson(CPacketBuilder::BuildLoginReq(...)); // 요청 전송
//   // WM_NET_NG_PUSH 등의 메시지가 MainTabDlg로 전달됨
//   net.Disconnect();
// ============================================================================

#include "ClientProtocol.h"
#include <vector>
#include <string>

// ── 커스텀 윈도우 메시지 정의 ─────────────────────────────────────────────
// WM_APP: Windows가 사용자 정의 메시지용으로 예약한 시작 번호
// 각 메시지의 WPARAM/LPARAM에 데이터 포인터를 담아서 전달합니다.

// 서버 접속 성공 시 UI에 전달 (연결 상태 표시 갱신용)
#define WM_NET_CONNECTED        (WM_APP + 100)
// 서버 접속 끊김 시 UI에 전달
#define WM_NET_DISCONNECTED     (WM_APP + 101)
// NG 검사 결과 푸시 수신 (프로토콜 110)
// LPARAM = new std::string(json) → 수신자가 delete 해야 함
#define WM_NET_NG_PUSH          (WM_APP + 102)
// OK/NG 카운트 푸시 수신 (프로토콜 112)
#define WM_NET_OK_COUNT_PUSH    (WM_APP + 103)
// 서버 헬스 상태 푸시 수신 (프로토콜 170)
#define WM_NET_HEALTH_PUSH      (WM_APP + 104)
// 로그인 응답 수신 (프로토콜 101)
#define WM_NET_LOGIN_RES        (WM_APP + 105)
// 기타 응답 수신 (범용)
// WPARAM = protocol_no, LPARAM = new std::string(json)
#define WM_NET_RESPONSE         (WM_APP + 106)
// 재학습 진행률 푸시 수신 (프로토콜 154)
#define WM_NET_RETRAIN_PROGRESS (WM_APP + 107)

// ============================================================================
// CNetworkClient 클래스
// ============================================================================
class CNetworkClient {
public:
    // 생성자: 멤버 변수 초기화
    CNetworkClient();

    // 소멸자: 연결 해제 및 리소스 정리
    ~CNetworkClient();

    // ── 연결 관리 ────────────────────────────────────────────────────────

    // Connect: 서버에 TCP 접속하고 수신 스레드를 시작
    // 파라미터:
    //   host       — 서버 IP 주소 (예: "10.10.10.130")
    //   port       — 서버 포트 번호 (예: 9010)
    //   hNotifyWnd — 패킷 수신 시 PostMessage를 보낼 윈도우 핸들
    //                (보통 MainTabDlg의 m_hWnd)
    // 반환값: true=접속 성공, false=실패
    bool Connect(const CString& host, UINT16 port, HWND hNotifyWnd);

    // Disconnect: 서버와의 연결을 끊고 수신 스레드를 종료
    void Disconnect();

    // IsConnected: 현재 서버와 연결되어 있는지 확인
    // 반환값: true=연결 중, false=미연결
    bool IsConnected() const;

    // ── 데이터 전송 ──────────────────────────────────────────────────────

    // Send: 이미 조립된 패킷(바이트 배열)을 서버로 전송
    // 파라미터: packet — BuildPacket()으로 생성한 바이트 배열
    // 반환값: true=전송 성공, false=실패
    // 참고: CriticalSection으로 보호되어 멀티스레드에서 안전
    bool Send(const std::vector<char>& packet);

    // SendJson: JSON 문자열을 패킷으로 변환하여 전송 (편의 함수)
    // 내부적으로 BuildPacket() + Send()를 호출
    bool SendJson(const CString& json);

private:
    // ── 소켓 ─────────────────────────────────────────────────────────────
    // SOCKET: Windows 소켓 핸들. INVALID_SOCKET이면 미연결 상태.
    SOCKET m_socket;

    // ── UI 알림 대상 ─────────────────────────────────────────────────────
    // 패킷 수신 시 PostMessage를 보낼 윈도우의 핸들
    HWND m_hNotifyWnd;

    // ── 수신 스레드 ──────────────────────────────────────────────────────
    // m_hRecvThread: 백그라운드 수신 스레드의 핸들
    HANDLE m_hRecvThread;
    // m_bRunning: 수신 스레드의 실행 여부 플래그
    // volatile: 다른 스레드에서 변경 시 즉시 반영되도록 최적화 방지
    volatile bool m_bRunning;

    // ── 스레드 안전 전송 ─────────────────────────────────────────────────
    // CRITICAL_SECTION: Windows의 뮤텍스 (상호 배제 잠금)
    // UI 스레드와 수신 스레드 모두에서 send()를 호출할 수 있으므로
    // 동시 접근을 방지하기 위해 사용합니다.
    CRITICAL_SECTION m_csSend;

    // ── 수신 스레드 함수 ─────────────────────────────────────────────────

    // RecvThreadProc: 스레드 진입점 (static — 클래스 인스턴스 없이 호출 가능해야 함)
    // _beginthreadex가 요구하는 함수 시그니처: unsigned __stdcall func(void*)
    // pParam에 this 포인터를 전달하여 인스턴스 메서드를 호출합니다.
    static unsigned __stdcall RecvThreadProc(void* pParam);

    // RecvLoop: 실제 수신 루프 (인스턴스 메서드)
    // 서버로부터 패킷을 계속 읽어서 OnPacketReceived로 전달
    void RecvLoop();

    // RecvN: 정확히 n바이트를 수신할 때까지 반복 읽기
    // TCP는 스트림 프로토콜이므로 한 번의 recv()로 원하는 만큼 못 받을 수 있음
    // 파라미터:
    //   buf — 수신 버퍼
    //   n   — 읽어야 할 정확한 바이트 수
    // 반환값: true=성공, false=연결 끊김 또는 에러
    bool RecvN(char* buf, int n);

    // OnPacketReceived: 하나의 완전한 패킷(JSON)을 수신했을 때 호출
    // JSON에서 protocol_no를 추출하여 적절한 WM_NET_xxx 메시지로 PostMessage
    void OnPacketReceived(const std::string& json);

    // SendAckIfNeeded: 수신한 메시지에 ACK가 필요한 경우 자동으로 ACK 전송
    void SendAckIfNeeded(int protocolNo, const std::string& json);

    // SendHeartbeat: 서버 세션 유지용 heartbeat 패킷 전송 (5초마다)
    // 서버의 recv 루프가 데이터를 받아서 세션을 유지합니다.
    void SendHeartbeat();
};
