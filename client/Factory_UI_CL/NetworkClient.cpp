// ============================================================================
// NetworkClient.cpp
// ============================================================================
// 목적:
//   메인서버(포트 9010)와 TCP 통신을 수행하는 네트워크 클라이언트 구현부입니다.
//   백그라운드 스레드에서 서버의 푸시 데이터를 수신하고,
//   PostMessage를 통해 UI 스레드에 안전하게 전달합니다.
//
// 핵심 개념:
//   - WinSock2: Windows에서 소켓 프로그래밍을 위한 API
//   - _beginthreadex: C 런타임 라이브러리와 호환되는 스레드 생성 함수
//   - PostMessage: 스레드 간 안전한 메시지 전달 (비동기)
//   - CRITICAL_SECTION: 공유 자원(소켓)에 대한 동시 접근 방지
// ============================================================================

#include "pch.h"
#include "NetworkClient.h"
#include "PacketBuilder.h"

#include <process.h>    // _beginthreadex — 스레드 생성 함수
#include <string>

// ============================================================================
// 생성자/소멸자
// ============================================================================

CNetworkClient::CNetworkClient()
    : m_socket(INVALID_SOCKET)    // 소켓 초기값: 아직 연결 안 됨
    , m_hNotifyWnd(nullptr)       // 알림 대상 윈도우: 미설정
    , m_hRecvThread(nullptr)      // 수신 스레드: 미생성
    , m_bRunning(false)           // 수신 루프: 중지 상태
{
    // CriticalSection 초기화
    // CriticalSection은 사용 전에 반드시 Initialize 해야 합니다.
    InitializeCriticalSection(&m_csSend);
}

CNetworkClient::~CNetworkClient()
{
    // 소멸 시 연결 해제
    Disconnect();

    // CriticalSection 리소스 해제
    DeleteCriticalSection(&m_csSend);
}

// ============================================================================
// Connect — 서버에 TCP 접속
// ============================================================================
// 동작:
//   1) WinSock2 초기화 (WSAStartup)
//   2) 소켓 생성 (socket)
//   3) 서버 주소 설정 및 접속 (connect)
//   4) 백그라운드 수신 스레드 시작
//   5) UI에 WM_NET_CONNECTED 메시지 전달
bool CNetworkClient::Connect(const CString& host, UINT16 port, HWND hNotifyWnd)
{
    // 이전 연결 상태 정리 (소켓 or 스레드가 남아있으면 정리)
    if (m_socket != INVALID_SOCKET || m_hRecvThread) {
        Disconnect();
    }

    m_hNotifyWnd = hNotifyWnd;

    // ※ WSAStartup은 Factory_UI_CL.cpp(앱 진입점)에서 1회만 호출합니다.
    //    여기서 중복 호출하면 WSACleanup 참조 카운트 불일치로 소켓이 무효화됩니다.

    // ── 소켓 생성 ──
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        TRACE(_T("[NetworkClient] socket() 실패: %d\n"), WSAGetLastError());
        return false;
    }

    // ── 소켓 옵션 설정 ──
    // SO_KEEPALIVE: TCP 레벨에서 주기적으로 생존 확인 패킷을 보냄
    //              → 상대방이 살아있는지 OS가 자동 확인, 끊어지면 recv 에러 반환
    //              → 방화벽/NAT의 유휴 연결 타임아웃도 방지
    BOOL keepAlive = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&keepAlive), sizeof(keepAlive));

    // TCP_NODELAY: Nagle 알고리즘 비활성화
    //             → 작은 패킷(JSON)도 즉시 전송 (지연 없이)
    BOOL noDelay = TRUE;
    setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    // SO_RCVTIMEO: recv() 타임아웃 5초 설정
    //             → recv가 5초마다 SOCKET_ERROR(WSAETIMEDOUT)를 반환
    //             → RecvLoop이 멈추지 않고 주기적으로 깨어남
    //             → 이를 이용해 heartbeat 패킷을 서버에 전송하여 세션 유지
    DWORD recvTimeout = 5000;  // 5초 (밀리초)
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));

    // ── 서버 주소 설정 ──
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    CStringA hostA(host);
    if (inet_pton(AF_INET, (LPCSTR)hostA, &serverAddr.sin_addr) <= 0) {
        TRACE(_T("[NetworkClient] IP 주소 파싱 실패: %s\n"), (LPCTSTR)host);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    // ── 서버에 접속 ──
    if (connect(m_socket, reinterpret_cast<sockaddr*>(&serverAddr),
        sizeof(serverAddr)) == SOCKET_ERROR)
    {
        TRACE(_T("[NetworkClient] connect() 실패: %d (서버 %s:%d)\n"),
            WSAGetLastError(), (LPCTSTR)host, port);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    TRACE(_T("[NetworkClient] 서버 접속 성공: %s:%d\n"), (LPCTSTR)host, port);

    // ── 수신 스레드 시작 ──
    m_bRunning = true;
    m_hRecvThread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, RecvThreadProc, this, 0, nullptr));

    if (!m_hRecvThread) {
        TRACE(_T("[NetworkClient] 수신 스레드 생성 실패\n"));
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        m_bRunning = false;
        return false;
    }

    // ── UI 스레드에 접속 성공 알림 ──
    if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd)) {
        ::PostMessage(m_hNotifyWnd, WM_NET_CONNECTED, 0, 0);
    }

    return true;
}

// ============================================================================
// Disconnect — 서버 연결 해제
// ============================================================================
void CNetworkClient::Disconnect()
{
    // 수신 루프 중단 플래그 설정
    m_bRunning = false;

    // 소켓 닫기 (recv()가 에러를 반환하여 RecvLoop가 종료됨)
    if (m_socket != INVALID_SOCKET) {
        // shutdown: 소켓의 송신/수신을 정상적으로 중단
        // SD_BOTH: 송신과 수신 모두 중단
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    // 수신 스레드 종료 대기
    if (m_hRecvThread) {
        // WaitForSingleObject: 스레드가 종료될 때까지 최대 3초 대기
        WaitForSingleObject(m_hRecvThread, 3000);
        CloseHandle(m_hRecvThread);
        m_hRecvThread = nullptr;
    }

    // ※ WSACleanup은 여기서 호출하지 않음!
    //    앱 종료 시 ExitInstance에서 1회만 호출합니다.
    //    여기서 호출하면 다른 소켓/재접속이 모두 무효화됩니다.

    // UI에 연결 끊김 알림
    if (m_hNotifyWnd && ::IsWindow(m_hNotifyWnd)) {
        ::PostMessage(m_hNotifyWnd, WM_NET_DISCONNECTED, 0, 0);
    }

    TRACE(_T("[NetworkClient] 서버 연결 해제\n"));
}

// ============================================================================
// IsConnected — 연결 상태 확인
// ============================================================================
bool CNetworkClient::IsConnected() const
{
    return (m_socket != INVALID_SOCKET) && m_bRunning;
}

// ============================================================================
// Send — 패킷 전송 (스레드 안전)
// ============================================================================
// CriticalSection으로 보호하여 여러 스레드에서 동시에 send()를 호출해도 안전합니다.
bool CNetworkClient::Send(const std::vector<char>& packet)
{
    if (m_socket == INVALID_SOCKET || packet.empty()) {
        return false;
    }

    // ── 임계 구역 진입 (잠금) ──
    // EnterCriticalSection: 다른 스레드가 이 구간에 있으면 여기서 대기
    EnterCriticalSection(&m_csSend);

    // send: 소켓을 통해 데이터 전송
    // 반환값: 실제로 보낸 바이트 수 (실패 시 SOCKET_ERROR)
    int totalSent = 0;
    int remaining = static_cast<int>(packet.size());
    const char* ptr = packet.data();

    // TCP는 한 번의 send()로 모든 데이터가 전송되지 않을 수 있으므로
    // 전체 데이터가 전송될 때까지 반복합니다.
    while (remaining > 0) {
        int sent = send(m_socket, ptr + totalSent, remaining, 0);
        if (sent == SOCKET_ERROR) {
            TRACE(_T("[NetworkClient] send() 실패: %d\n"), WSAGetLastError());
            LeaveCriticalSection(&m_csSend);
            return false;
        }
        totalSent += sent;
        remaining -= sent;
    }

    // ── 임계 구역 해제 (잠금 풀기) ──
    LeaveCriticalSection(&m_csSend);
    return true;
}

// ============================================================================
// SendJson — JSON 문자열을 패킷으로 변환하여 전송 (편의 함수)
// ============================================================================
bool CNetworkClient::SendJson(const CString& json)
{
    // JSON → 패킷(헤더+본문) 변환 후 전송
    std::vector<char> packet = CPacketBuilder::BuildPacket(json);
    return Send(packet);
}

// ============================================================================
// RecvThreadProc — 수신 스레드 진입점 (static)
// ============================================================================
// _beginthreadex가 호출하는 함수입니다.
// pParam에 CNetworkClient의 this 포인터가 전달됩니다.
unsigned __stdcall CNetworkClient::RecvThreadProc(void* pParam)
{
    // void* → CNetworkClient* 캐스팅
    CNetworkClient* pThis = reinterpret_cast<CNetworkClient*>(pParam);

    // 인스턴스의 RecvLoop 호출 (실제 수신 작업)
    pThis->RecvLoop();

    return 0;  // 스레드 정상 종료
}

// ============================================================================
// RecvLoop — 수신 루프 (백그라운드 스레드에서 실행)
// ============================================================================
// 동작:
//   1) 4바이트 헤더 수신 시도 (5초 타임아웃)
//   2) 타임아웃 시 → heartbeat 전송하여 서버 세션 유지
//   3) 데이터 수신 시 → JSON 파싱 → OnPacketReceived
//   4) 실제 연결 끊김 시 → 루프 종료 → WM_NET_DISCONNECTED
//
// 핵심: SO_RCVTIMEO(5초) 덕분에 recv가 주기적으로 깨어나서
//       heartbeat를 보낼 수 있습니다. 서버의 recv도 데이터를 받으므로
//       세션이 유지됩니다.
void CNetworkClient::RecvLoop()
{
    TRACE(_T("[NetworkClient] 수신 스레드 시작\n"));

    while (m_bRunning) {
        // ── 1단계: 4바이트 헤더 수신 (5초 타임아웃) ──
        char header[factory_client::HEADER_SIZE];
        int got = recv(m_socket, header, factory_client::HEADER_SIZE, 0);

        if (got == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                // ── recv 타임아웃 (5초 경과, 서버가 아무것도 안 보냄) ──
                // heartbeat(EXT_ACK)를 보내서 서버 세션을 유지합니다.
                // 서버의 handle_client는 이 패킷을 recv로 받아서 루프를 계속합니다.
                SendHeartbeat();
                continue;  // 다시 수신 대기
            }
            // 실제 소켓 에러 (연결 끊김)
            TRACE(_T("[NetworkClient] recv 에러: %d\n"), err);
            break;
        }

        if (got == 0) {
            // 서버가 정상적으로 연결을 종료함
            TRACE(_T("[NetworkClient] 서버가 연결 종료\n"));
            break;
        }

        // got이 4바이트 미만이면 나머지를 더 읽기
        if (got < factory_client::HEADER_SIZE) {
            if (!RecvN(header + got, factory_client::HEADER_SIZE - got)) {
                break;
            }
        }

        // ── 2단계: 헤더에서 JSON 크기 추출 ──
        UINT32 jsonSize = 0;
        if (!CPacketBuilder::ParseHeader(header, jsonSize)) {
            TRACE(_T("[NetworkClient] 비정상 헤더 (size=%u)\n"), jsonSize);
            break;
        }

        // ── 3단계: JSON 본문 수신 ──
        std::string jsonBuf(jsonSize, '\0');
        if (!RecvN(&jsonBuf[0], static_cast<int>(jsonSize))) {
            break;
        }

        // ── 4단계: 패킷 처리 ──
        OnPacketReceived(jsonBuf);
    }

    // 수신 루프 종료
    TRACE(_T("[NetworkClient] 수신 스레드 종료 (running=%d)\n"), (int)m_bRunning);

    // ※ 소켓은 여기서 닫지 않음! Disconnect()가 관리합니다.
    m_bRunning = false;

    // ※ WM_NET_DISCONNECTED는 Disconnect()에서만 발송합니다.
    //    여기서 중복 발송하면 MainTabDlg의 재접속 타이머가 2번 등록되어
    //    IDT_RECONNECT가 중복 실행되는 부작용이 생깁니다.
    //    Disconnect()가 소켓을 닫아 이 루프를 종료시키므로
    //    이후 Disconnect() 내부의 PostMessage가 반드시 호출됩니다.
}

// ============================================================================
// RecvN — 정확히 n바이트 수신 (TCP 스트림 대응)
// ============================================================================
// TCP는 "스트림" 프로토콜입니다. 즉, send(100바이트)를 해도
// recv()가 한 번에 100바이트를 주지 않을 수 있습니다.
// 예) 첫 번째 recv() → 60바이트, 두 번째 recv() → 40바이트
// 따라서 원하는 만큼 받을 때까지 반복해야 합니다.
bool CNetworkClient::RecvN(char* buf, int n)
{
    int totalRecv = 0;  // 지금까지 받은 바이트 수

    while (totalRecv < n) {
        // recv: 소켓에서 데이터 수신
        // 반환값: 받은 바이트 수. 0이면 상대방이 연결 종료. 음수면 에러.
        int got = recv(m_socket, buf + totalRecv, n - totalRecv, 0);

        if (got <= 0) {
            // got == 0: 서버가 정상적으로 연결 종료
            // got < 0: 에러 발생 (SOCKET_ERROR)
            return false;
        }

        totalRecv += got;
    }

    return true;
}

// ============================================================================
// SendHeartbeat — 서버 세션 유지를 위한 heartbeat 패킷 전송
// ============================================================================
// 목적: 서버의 handle_client()는 클라이언트로부터 패킷을 recv로 대기합니다.
//       클라이언트가 오랫동안 아무것도 안 보내면 서버가 세션을 끊을 수 있습니다.
//       5초마다 간단한 EXT_ACK(190) 패킷을 보내서 "나 아직 살아있어!"를 알립니다.
//       서버는 이 패킷을 받으면 로그만 출력하고 다음 recv로 돌아갑니다.
void CNetworkClient::SendHeartbeat()
{
    CString hbJson = CPacketBuilder::BuildAck(
        factory_client::EXT_ACK, _T("heartbeat"));
    SendJson(hbJson);
}

// ============================================================================
// OnPacketReceived — 수신한 패킷을 파싱하여 UI 스레드에 전달
// ============================================================================
// 동작:
//   1) JSON에서 protocol_no 추출
//   2) 프로토콜 번호에 따라 적절한 WM_NET_xxx 메시지로 PostMessage
//   3) ACK가 필요한 메시지는 자동으로 ACK 전송
//
// 중요: PostMessage는 비동기이므로 JSON 데이터를 힙에 할당(new)하여 전달하고,
//       UI 스레드의 메시지 핸들러에서 delete로 해제해야 합니다.
void CNetworkClient::OnPacketReceived(const std::string& json)
{
    CStringA jsonA(json.c_str());

    // protocol_no 추출 — 이 패킷이 어떤 종류의 메시지인지 판별
    int protocolNo = CPacketBuilder::ExtractInt(jsonA, "protocol_no");

    TRACE(_T("[NetworkClient] 수신: protocol_no=%d, size=%d\n"),
        protocolNo, (int)json.size());

    // ACK가 필요한 메시지에 대해 자동 ACK 전송
    SendAckIfNeeded(protocolNo, json);

    // UI 윈도우가 유효하지 않으면 무시
    if (!m_hNotifyWnd || !::IsWindow(m_hNotifyWnd)) {
        return;
    }

    // ── 프로토콜 번호별 메시지 라우팅 ──
    // new std::string: 힙에 JSON 데이터를 복사하여 UI 스레드에 전달
    // UI 스레드의 메시지 핸들러가 이 포인터를 delete 해야 메모리 누수가 없습니다!
    switch (protocolNo) {
    case factory_client::INSPECT_NG_PUSH:
        // NG 검사 결과 푸시 → MainTabDlg가 검사 데이터 갱신
        ::PostMessage(m_hNotifyWnd, WM_NET_NG_PUSH, 0,
            reinterpret_cast<LPARAM>(new std::string(json)));
        break;

    case factory_client::INSPECT_OK_COUNT_PUSH:
        // OK/NG 카운트 푸시 → 종합 현황 갱신
        ::PostMessage(m_hNotifyWnd, WM_NET_OK_COUNT_PUSH, 0,
            reinterpret_cast<LPARAM>(new std::string(json)));
        break;

    case factory_client::SERVER_HEALTH_PUSH:
        // 서버 헬스 상태 변경 → 서버 LED 업데이트
        ::PostMessage(m_hNotifyWnd, WM_NET_HEALTH_PUSH, 0,
            reinterpret_cast<LPARAM>(new std::string(json)));
        break;

    case factory_client::LOGIN_RES:
        // 로그인 응답 → 인증 결과 처리
        ::PostMessage(m_hNotifyWnd, WM_NET_LOGIN_RES, 0,
            reinterpret_cast<LPARAM>(new std::string(json)));
        break;

    case factory_client::RETRAIN_PROGRESS_PUSH:
        // 재학습 진행률 → 모델 페이지 진행바 업데이트
        ::PostMessage(m_hNotifyWnd, WM_NET_RETRAIN_PROGRESS, 0,
            reinterpret_cast<LPARAM>(new std::string(json)));
        break;

    default:
        // 그 외 응답 (INSPECT_HISTORY_RES, STATS_RES, MODEL_LIST_RES 등)
        // WPARAM에 프로토콜 번호를 담아서 핸들러가 구분할 수 있게 함
        ::PostMessage(m_hNotifyWnd, WM_NET_RESPONSE,
            static_cast<WPARAM>(protocolNo),
            reinterpret_cast<LPARAM>(new std::string(json)));
        break;
    }
}

// ============================================================================
// SendAckIfNeeded — ACK 자동 전송
// ============================================================================
// 서버가 보낸 메시지 중 ACK가 필요한 것들에 대해 자동으로 응답합니다.
// 예) INSPECT_NG_PUSH(110) 수신 → INSPECT_NG_ACK_EXT(111) 자동 전송
void CNetworkClient::SendAckIfNeeded(int protocolNo, const std::string& json)
{
    // ACK 필요 여부 확인
    if (!factory_client::RequiresAck(protocolNo)) {
        return;  // ACK 불필요한 메시지는 무시
    }

    // JSON에서 inspection_id 추출 (ACK에 포함시켜야 함)
    CStringA jsonA(json.c_str());
    CStringA inspId = CPacketBuilder::ExtractString(jsonA, "inspection_id");

    // 대응하는 ACK 번호 계산
    int ackNo = factory_client::AckNoFor(protocolNo);

    // ACK JSON 생성 및 전송
    CString ackJson = CPacketBuilder::BuildAck(ackNo, CString(inspId));
    SendJson(ackJson);

    TRACE(_T("[NetworkClient] ACK 전송: %d → %d (id=%S)\n"),
        protocolNo, ackNo, (LPCSTR)inspId);
}
