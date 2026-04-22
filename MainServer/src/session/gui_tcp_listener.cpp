// ============================================================================
// gui_tcp_listener.cpp — GUI 클라이언트 TCP accept/recv 루프
// ============================================================================
// 책임:
//   GUI 클라이언트(MFC) 용 TCP 포트(기본 9010)를 열고 연결을 수락한다.
//   각 연결마다 스레드를 띄워 프레임 단위로 JSON 요청을 수신하고,
//   수신한 요청은 GuiRouter 에 위임한다.
//
// 역할 분담:
//   [GuiTcpListener] → 소켓/accept/recv/프레이밍
//   [GuiRouter]      → 프로토콜 번호별 분기 + 비즈니스 처리
//   [GuiService]     → DB/DAO 호출
//
// 프레이밍 규약:
//   [4바이트 Big-Endian JSON 길이] + [JSON 본문]
//   AiServer 의 PacketReader, 추론서버의 TcpListener 와 동일한 프로토콜.
//
// 스레드 모델:
//   - 하나의 accept 스레드가 연결을 계속 수락
//   - 각 연결마다 별도 핸들러 스레드(detach) — 최대 20개 동시 세션
//   - SO_RCVTIMEO 로 idle 30초 타임아웃 + 1초 accept 타임아웃(종료 신호 체크)
// ============================================================================
#include "session/gui_tcp_listener.h"
#include "session/session_manager.h"
#include "core/logger.h"
#include "Protocol.h"

#include <cstring>

// 플랫폼별 소켓 헤더/매크로 — Linux 배포가 기본, Windows 빌드 호환을 위한 분기
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define CLOSE_SOCK closesocket
  using socklen_t = int;
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCK ::close
#endif

namespace factory {

// ---------------------------------------------------------------------------
// 생성자 — 포트/EventBus/Router 를 저장만 하고 소켓은 start() 에서 연다.
// is_running_ 은 atomic<bool>: accept 루프와 stop() 사이의 신호용.
// ---------------------------------------------------------------------------
GuiTcpListener::GuiTcpListener(EventBus& bus, uint16_t port, GuiRouter& router)
    : event_bus_(bus),
      listen_port_(port),
      server_fd_(-1),
      is_running_(false),
      router_(router) {
}

GuiTcpListener::~GuiTcpListener() {
    // RAII: 소멸 시 자동 종료 — 명시적 stop() 호출이 없었을 때 안전망
    stop();
}

// ---------------------------------------------------------------------------
// start — 리슨 소켓 열고 accept 스레드 시작
//
// 주요 소켓 옵션:
//   SO_REUSEADDR : 종료 직후 재시작 시 "Address already in use" 방지
//   SO_RCVTIMEO 1초 : accept 호출이 1초마다 깨어나 is_running_ 체크 → 종료 가능
//   listen backlog 32 : 동시 수락 대기 큐 깊이
//
// 중복 start 방지:
//   exchange(true) — 이미 true 면 early return (atomic 으로 원자적 검사+설정)
// ---------------------------------------------------------------------------
void GuiTcpListener::start() {
    if (is_running_.exchange(true)) return;  // 이미 실행 중이면 noop

    server_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd_ < 0) {
        log_err_clt("소켓 생성 실패 | GUI 리스너");
        is_running_.store(false);
        return;
    }

    // 재시작 시 포트 점유 이슈 방지
    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    // accept 에 1초 타임아웃 → is_running_=false 가 되면 루프가 즉시 탈출
    struct timeval tv{1, 0};
    ::setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;     // 모든 네트워크 인터페이스에서 수신
    addr.sin_port        = htons(listen_port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_err_clt("바인드 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 32) < 0) {
        log_err_clt("리슨 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    // accept 루프는 별도 스레드에서 실행 — 메인은 블로킹되지 않음
    accept_thread_ = std::thread(&GuiTcpListener::run_accept_loop, this);
    log_clt("GUI 리스너 시작 | 포트=%d", listen_port_);
}

// ---------------------------------------------------------------------------
// stop — accept 루프에 종료 신호 + 리스닝 소켓 정리 + 스레드 join
// 주의: 이 함수는 accept 스레드 스스로 호출되면 데드락 발생 → 외부에서만 호출.
// ---------------------------------------------------------------------------
void GuiTcpListener::stop() {
    if (!is_running_.exchange(false)) return;  // 이미 정지됨
    if (server_fd_ >= 0) {
        CLOSE_SOCK(server_fd_);   // accept 가 EBADF/EINVAL 로 풀려남
        server_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

// ---------------------------------------------------------------------------
// run_accept_loop — 새 클라이언트 연결 수락 루프 (accept 스레드에서 실행)
//
// 각 연결마다:
//   1) IP:PORT 문자열 생성 → SessionManager 키로 사용
//   2) 동시 접속 수 체크 (최대 20) — 초과 시 즉시 close
//   3) 별도 스레드 detach 로 handle_client 시작
//
// detach 이유:
//   세션 수가 변동적이고 수명이 제각각이라 join 기반 관리가 복잡.
//   handle_client 가 자체적으로 정리(세션 해제 + close) 하므로 detach 안전.
// ---------------------------------------------------------------------------
void GuiTcpListener::run_accept_loop() {
    while (is_running_.load()) {
        sockaddr_in client_addr{};
        socklen_t   addr_len = sizeof(client_addr);
        int client_fd = static_cast<int>(
            ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len));
        if (client_fd < 0) continue;  // 타임아웃/EINTR — 루프 계속

        // IP 주소를 문자열로 변환 — ConnectionRegistry/SessionManager 키로 통일
        char ip_buf[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::string remote_addr = std::string(ip_buf) + ":" +
                                  std::to_string(ntohs(client_addr.sin_port));

        // 동시 GUI 접속 수 제한 — DoS/리소스 고갈 방어
        // 20 은 config.limits.max_gui_sessions 와 동기화 (하드코딩 주의)
        if (SessionManager::instance().session_count() >= 20) {
            log_err_clt("GUI 최대 접속 수 초과");
            CLOSE_SOCK(client_fd);
            continue;
        }

        // detach: 각 클라이언트 수명은 독립 — 스레드 소멸도 스스로 처리
        std::thread(&GuiTcpListener::handle_client, this, client_fd, remote_addr).detach();
    }
}

// ---------------------------------------------------------------------------
// handle_client — 한 클라이언트 세션의 수신 루프
//
// 수명:
//   1) recv 타임아웃 30초 세팅 (slow-loris/heartbeat 부재 방어)
//   2) SessionManager 등록
//   3) 프레임 단위로 JSON 수신 → router 위임 반복
//   4) 연결 종료 시 SessionManager 해제 + 소켓 close
//
// 연결 종료 조건:
//   - recv_one_request 가 false (소켓 끊김/타임아웃/크기 초과)
//   - is_running_ 가 false (서버 종료)
//   - SessionManager::force_close 등 외부 신호로 소켓이 닫힘 (중복 로그인 교체 시)
// ---------------------------------------------------------------------------
void GuiTcpListener::handle_client(int client_fd, const std::string& remote_addr) {
    // recv 타임아웃 30초 — heartbeat 미수신/좀비 연결 정리
    struct timeval client_tv{30, 0};
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&client_tv), sizeof(client_tv));

    SessionManager::instance().register_session(client_fd, remote_addr);

    while (is_running_.load()) {
        std::string json_request;
        if (!recv_one_request(client_fd, json_request)) break;  // 수신 실패 → 종료
        router_.route(client_fd, remote_addr, json_request);
    }

    SessionManager::instance().unregister_session(client_fd);
    CLOSE_SOCK(client_fd);
}

// ── 패킷 수신 ────────────────────────────────────────────────────────────
// TCP 는 스트림 프로토콜이므로 recv 한 번으로 N바이트가 통으로 오지 않을 수 있다.
// recv_n 은 부족하면 반복 호출해서 정확히 N바이트를 채운다.
// 반환 false: 소켓 EOF(상대가 close), 에러, 또는 타임아웃
// ---------------------------------------------------------------------------
static bool recv_n(int fd, void* buf, std::size_t n) {
    std::size_t total = 0;
    auto* p = static_cast<char*>(buf);
    while (total < n) {
        int got = static_cast<int>(::recv(fd, p + total, static_cast<int>(n - total), 0));
        if (got <= 0) return false;   // 0 = EOF, <0 = 에러/타임아웃
        total += static_cast<std::size_t>(got);
    }
    return true;
}

// ---------------------------------------------------------------------------
// recv_one_request — 한 JSON 프레임 완전 수신
// 프레임: [4바이트 BE length][length 바이트의 JSON 본문]
//
// 크기 검증:
//   0     : 프로토콜 위반 (빈 프레임)
//   >64KB : config.limits.json_max_bytes 와 동일 — 비정상 대용량 차단
// ---------------------------------------------------------------------------
bool GuiTcpListener::recv_one_request(int client_fd, std::string& out_json) {
    uint8_t header[HEADER_SIZE];
    if (!recv_n(client_fd, header, HEADER_SIZE)) return false;

    // Big-Endian 4바이트 → uint32_t (htonl 의 수동 구현)
    uint32_t json_size = (uint32_t)header[0] << 24 |
                         (uint32_t)header[1] << 16 |
                         (uint32_t)header[2] << 8  |
                         (uint32_t)header[3];

    // 크기 검증 — 외부 입력이므로 반드시 상한/하한 체크
    if (json_size == 0 || json_size > 64 * 1024) return false;

    out_json.assign(json_size, '\0');
    if (!recv_n(client_fd, out_json.data(), json_size)) return false;
    return true;
}

} // namespace factory
