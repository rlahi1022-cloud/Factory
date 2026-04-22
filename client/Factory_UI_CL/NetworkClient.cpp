// ============================================================================
// NetworkClient.cpp — MainServer GUI 포트(9010) 전용 TCP 클라이언트
// ============================================================================
// 책임:
//   - 서버 접속/해제 (Connect / Disconnect)
//   - 프레임 송수신: [4바이트 BE 길이] + [JSON 본문] (+ [바이너리 페이로드])
//   - 수신 백그라운드 스레드 운영 (RecvLoop)
//   - 서버 푸시 이벤트를 UI 스레드로 PostMessage 전달
//   - 주기적 heartbeat (EXT_ACK) 전송으로 세션 유지
//
// 스레드 모델:
//   UI 스레드  — Send*, Disconnect, 파라미터 설정
//   Recv 스레드 — RecvLoop 무한 루프 (select 타임아웃 5초 기반)
//   m_csSend CRITICAL_SECTION 으로 Send 경쟁 차단.
//
// 수신 이벤트 라우팅:
//   RecvLoop 가 프로토콜 번호를 파싱해 해당하는 WM_NET_* 메시지를
//   m_hNotifyWnd (MainTabDlg) 로 PostMessage → 메시지맵 핸들러가 처리.
//     110 INSPECT_NG_PUSH         → WM_NET_NG_PUSH (+ 이미지 바이너리 포인터)
//     112 INSPECT_OK_COUNT_PUSH   → WM_NET_OK_COUNT_PUSH
//     115 INSPECT_HISTORY_RES     → WM_NET_RESPONSE (범용)
//     117 INSPECT_IMAGE_RES       → WM_NET_NG_IMAGE (바이너리 동봉)
//     151 MODEL_LIST_RES          → WM_NET_RESPONSE
//     153 RETRAIN_RES             → WM_NET_RESPONSE
//     154 RETRAIN_PROGRESS_PUSH   → WM_NET_RETRAIN_PROGRESS
//     170 SERVER_HEALTH_PUSH      → WM_NET_HEALTH_PUSH
//
// 보안/안정성:
//   - recv_n 으로 TCP 스트림에서 정확한 바이트 수 보장
//   - JSON 크기 상한 64KB / 이미지 블록 상한 50MB — 비정상 입력 차단
//   - RecvLoop 에서 에러 감지 시 WM_NET_DISCONNECTED 발송 → UI 자동 복구
//
// 대응 서버 모듈: MainServer/src/session/gui_tcp_listener.cpp + gui_router.cpp
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
    BOOL keepAlive = TRUE;
    if (setsockopt(m_socket, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&keepAlive), sizeof(keepAlive)) == SOCKET_ERROR) {
        TRACE(_T("[NetworkClient] SO_KEEPALIVE 설정 실패: %d\n"), WSAGetLastError());
    }

    // v0.14.2: Windows TCP Keepalive 간격을 명시적으로 설정 (SIO_KEEPALIVE_VALS).
    //   기본값: onoff=1, keepalivetime=2시간, keepaliveinterval=1초
    //   → 우리는 30초 idle 후 5초 간격 probe 로 짧게.
    //   Windows 는 Linux 처럼 TCP_KEEPIDLE/INTVL/CNT 를 직접 쓸 수 없어 WSAIoctl 사용.
    //   이렇게 안 하면 SO_KEEPALIVE 만 켜진 상태 = OS 기본 2시간 → 사실상 무력.
    struct tcp_keepalive {
        ULONG onoff;
        ULONG keepalivetime;     // ms — 첫 probe 까지 idle 시간
        ULONG keepaliveinterval; // ms — probe 간격
    } ka;
    ka.onoff             = 1;
    ka.keepalivetime     = 30 * 1000;   // 30초 idle
    ka.keepaliveinterval = 5  * 1000;   // 5초 간격 probe
    DWORD bytesReturned = 0;
    // WSAIoctl 정의 (mstcpip.h 가 없으면 매크로로 대체 — 대부분 MFC 빌드엔 포함됨)
    constexpr DWORD SIO_KEEPALIVE_VALS_LOCAL = 0x98000004; // _WSAIOW(IOC_VENDOR, 4)
    if (WSAIoctl(m_socket, SIO_KEEPALIVE_VALS_LOCAL,
                 &ka, sizeof(ka),
                 nullptr, 0,
                 &bytesReturned,
                 nullptr, nullptr) == SOCKET_ERROR) {
        TRACE(_T("[NetworkClient] SIO_KEEPALIVE_VALS 설정 실패: %d\n"),
              WSAGetLastError());
    } else {
        TRACE(_T("[NetworkClient] TCP Keepalive: idle=30s probe=5s 적용\n"));
    }

    BOOL noDelay = TRUE;
    if (setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&noDelay), sizeof(noDelay)) == SOCKET_ERROR) {
        TRACE(_T("[NetworkClient] TCP_NODELAY 설정 실패: %d\n"), WSAGetLastError());
    }

    // v0.14.2: 수신 버퍼 8MB — 3MB NG 이미지 들어오는 동안 UI 스레드가
    //   다른 일 처리 중이어도 OS 가 버퍼에 담아줘서 서버 send 가 블록되지 않음.
    //   Windows 기본값(보통 64KB) 으로는 대용량 수신 중 서버가 먼저 끊는 현상 발생.
    int rcvbuf = 8 * 1024 * 1024;
    int sndbuf = 1 * 1024 * 1024;   // 클라→서버는 heartbeat/업로드 정도라 1MB 면 충분
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));

    // SO_RCVTIMEO: recv() 타임아웃 5초 → heartbeat 주기
    DWORD recvTimeout = 5000;
    if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout)) == SOCKET_ERROR) {
        TRACE(_T("[NetworkClient] SO_RCVTIMEO 설정 실패: %d\n"), WSAGetLastError());
    }

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

    // 패킷 크기 검증 — 2GB 초과 차단 (int 오버플로우 방지)
    if (packet.size() > static_cast<size_t>(INT_MAX)) {
        LeaveCriticalSection(&m_csSend);
        return false;
    }

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

        // ── 2단계: 헤더에서 JSON 크기 추출 (ParseHeader가 64KB 상한 검증) ──
        UINT32 jsonSize = 0;
        if (!CPacketBuilder::ParseHeader(header, jsonSize)) {
            TRACE(_T("[NetworkClient] 비정상 헤더 (size=%u)\n"), jsonSize);
            break;
        }

        // ── 3단계: JSON 본문 수신 (bad_alloc 방어) ──
        std::string jsonBuf;
        try {
            jsonBuf.assign(jsonSize, '\0');
        } catch (const std::bad_alloc&) {
            TRACE(_T("[NetworkClient] JSON 버퍼 할당 실패 (size=%u)\n"), jsonSize);
            break;
        }
        if (!RecvN(&jsonBuf[0], static_cast<int>(jsonSize))) {
            break;
        }

        // ── 4단계: 이미지 바이너리 수신 (v0.9.0+: 원본/히트맵/마스크 3장) ──
        // 서버 와이어 포맷 (gui_notifier.cpp on_gui_push):
        //   [JSON] + [원본 JPEG][히트맵 PNG][마스크 PNG]
        //   각 크기는 JSON의 image_size / heatmap_size / pred_mask_size
        //   어느 size든 0이면 해당 이미지 생략(하위호환)
        CStringA jsonA(jsonBuf.c_str());
        int imageSize    = CPacketBuilder::ExtractInt(jsonA, "image_size");
        int heatmapSize  = CPacketBuilder::ExtractInt(jsonA, "heatmap_size");
        int predMaskSize = CPacketBuilder::ExtractInt(jsonA, "pred_mask_size");
        const int totalBin = imageSize + heatmapSize + predMaskSize;

        std::vector<BYTE> imgBytes, heatBytes, maskBytes;

        if (totalBin > 0) {
            // 개별 최대 50MB 상한 — 메모리 폭주 방지
            constexpr int MAX_ONE = 50 * 1024 * 1024;
            if (imageSize    < 0 || imageSize    > MAX_ONE ||
                heatmapSize  < 0 || heatmapSize  > MAX_ONE ||
                predMaskSize < 0 || predMaskSize > MAX_ONE) {
                TRACE(_T("[NetworkClient] 비정상 이미지 크기 (img=%d heat=%d mask=%d)\n"),
                      imageSize, heatmapSize, predMaskSize);
                break;
            }
            // 순차 수신 — 서버 송신 순서와 동일 (원본 → 히트맵 → 마스크)
            auto recv_to = [this](std::vector<BYTE>& buf, int sz) -> bool {
                if (sz <= 0) return true;
                try { buf.resize(sz); } catch (const std::bad_alloc&) { return false; }
                return RecvN(reinterpret_cast<char*>(buf.data()), sz);
            };
            if (!recv_to(imgBytes,  imageSize))    { break; }
            if (!recv_to(heatBytes, heatmapSize))  { break; }
            if (!recv_to(maskBytes, predMaskSize)) { break; }
        }

        // ── 5단계: JSON 패킷 처리 (프로토콜 분기 + UI 이벤트 발송) ──
        OnPacketReceived(jsonBuf);

        // ── 6단계: 이미지가 동반된 경우 UI로 전달 ──
        // 프로토콜 110(INSPECT_NG_PUSH: 실시간) 또는 117(INSPECT_IMAGE_RES: 이력 on-demand)
        // 두 경우 모두 와이어 포맷이 동일하므로 같은 경로로 UI에 전달.
        // 힙 할당 후 PostMessage — UI 스레드가 수신 후 delete 책임.
        int protocolNo = CPacketBuilder::ExtractInt(jsonA, "protocol_no");
        const bool isNgImage    = (protocolNo == factory_client::INSPECT_NG_PUSH);
        const bool isHistImage  = (protocolNo == factory_client::INSPECT_IMAGE_RES);
        if ((isNgImage || isHistImage) && totalBin > 0 &&
            m_hNotifyWnd && ::IsWindow(m_hNotifyWnd)) {
            auto* pkt = new (std::nothrow) NgImagePacket{};
            if (pkt) {
                pkt->station_id    = CPacketBuilder::ExtractInt(jsonA, "station_id");
                pkt->inspection_id = CPacketBuilder::ExtractInt(jsonA, "inspection_id");
                pkt->image      = std::move(imgBytes);
                pkt->heatmap    = std::move(heatBytes);
                pkt->pred_mask  = std::move(maskBytes);
                if (!::PostMessage(m_hNotifyWnd, WM_NET_NG_IMAGE, 0,
                                   reinterpret_cast<LPARAM>(pkt))) {
                    delete pkt;  // PostMessage 실패 시 즉시 해제
                }
            }
        }
    }

    // ── 루프 종료 이유 판별 ──
    //  1) Disconnect() 호출: 이전에 m_bRunning=false + socket close → while 조건이 false로 바뀜
    //  2) Silent drop (서버 측 끊기, 네트워크 끊김): recv 에러로 break → m_bRunning이 여전히 true
    bool silent_drop = m_bRunning.load();

    TRACE(_T("[NetworkClient] 수신 스레드 종료 (silent_drop=%d)\n"), silent_drop);
    m_bRunning = false;

    // Silent drop인 경우에만 UI에 알림
    // (Disconnect()가 호출된 경우에는 Disconnect()가 직접 발송함 → 중복 방지)
    if (silent_drop && m_hNotifyWnd && ::IsWindow(m_hNotifyWnd)) {
        ::PostMessage(m_hNotifyWnd, WM_NET_DISCONNECTED, 0, 0);
    }
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
    if (json.empty()) return;

    CStringA jsonA(json.c_str());

    // protocol_no 추출
    int protocolNo = CPacketBuilder::ExtractInt(jsonA, "protocol_no");

    // 민감정보 노출 방지 — JSON 본문은 로그하지 않고 메타데이터만 출력
    TRACE(_T("[NetworkClient] 수신: protocol_no=%d, size=%d\n"),
        protocolNo, (int)json.size());

    // ACK가 필요한 메시지에 대해 자동 ACK 전송
    SendAckIfNeeded(protocolNo, json);

    // UI 윈도우가 유효하지 않으면 무시
    if (!m_hNotifyWnd || !::IsWindow(m_hNotifyWnd)) {
        return;
    }

    // PostMessage 안전 전송 헬퍼 — 실패 시 메모리 누수 방지
    auto safePost = [this](UINT msg, WPARAM wp, const std::string& data) {
        auto* pStr = new (std::nothrow) std::string(data);
        if (!pStr) return;
        if (!::PostMessage(m_hNotifyWnd, msg, wp, reinterpret_cast<LPARAM>(pStr))) {
            delete pStr;  // PostMessage 실패 시 즉시 해제
        }
    };

    switch (protocolNo) {
    case factory_client::INSPECT_NG_PUSH:
        safePost(WM_NET_NG_PUSH, 0, json);
        break;
    case factory_client::INSPECT_OK_COUNT_PUSH:
        safePost(WM_NET_OK_COUNT_PUSH, 0, json);
        break;
    case factory_client::SERVER_HEALTH_PUSH:
        safePost(WM_NET_HEALTH_PUSH, 0, json);
        break;
    case factory_client::LOGIN_RES:
        safePost(WM_NET_LOGIN_RES, 0, json);
        break;
    case factory_client::REGISTER_RES:
        safePost(WM_NET_REGISTER_RES, 0, json);
        break;
    case factory_client::RETRAIN_PROGRESS_PUSH:
        safePost(WM_NET_RETRAIN_PROGRESS, 0, json);
        break;
    default:
        safePost(WM_NET_RESPONSE, static_cast<WPARAM>(protocolNo), json);
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

    // inspection_id 형식 검증 — "stationN-YYYYMMDD..." 패턴
    // 길이 제한 (최대 128자) + 위험 문자 차단 (injection 방지)
    if (inspId.IsEmpty() || inspId.GetLength() > 128) {
        TRACE(_T("[NetworkClient] 비정상 inspection_id — ACK 생략 (len=%d)\n"),
            inspId.GetLength());
        return;
    }
    for (int i = 0; i < inspId.GetLength(); ++i) {
        char c = inspId[i];
        // 영숫자, '-', '_', '.'만 허용
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) {
            TRACE(_T("[NetworkClient] inspection_id 위험문자 — ACK 생략\n"));
            return;
        }
    }

    // 대응하는 ACK 번호 계산
    int ackNo = factory_client::AckNoFor(protocolNo);

    // ACK JSON 생성 및 전송
    CString ackJson = CPacketBuilder::BuildAck(ackNo, CString(inspId));
    SendJson(ackJson);

    TRACE(_T("[NetworkClient] ACK 전송: %d → %d (len=%d)\n"),
        protocolNo, ackNo, inspId.GetLength());
}
