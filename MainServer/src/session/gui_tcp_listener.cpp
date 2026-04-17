// ============================================================================
// gui_tcp_listener.cpp — TCP 수신만 담당 (라우팅은 GuiRouter에 위임)
// ============================================================================
#include "session/gui_tcp_listener.h"
#include "session/session_manager.h"
#include "core/logger.h"
#include "Protocol.h"

#include <cstring>

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

GuiTcpListener::GuiTcpListener(EventBus& bus, uint16_t port, GuiRouter& router)
    : event_bus_(bus),
      listen_port_(port),
      server_fd_(-1),
      is_running_(false),
      router_(router) {
}

GuiTcpListener::~GuiTcpListener() {
    stop();
}

void GuiTcpListener::start() {
    if (is_running_.exchange(true)) return;

    server_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd_ < 0) {
        log_err_clt("소켓 생성 실패 | GUI 리스너");
        is_running_.store(false);
        return;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct timeval tv{1, 0};
    ::setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(listen_port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_err_clt("바인드 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 8) < 0) {
        log_err_clt("리슨 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    accept_thread_ = std::thread(&GuiTcpListener::run_accept_loop, this);
    log_clt("GUI 리스너 시작 | 포트=%d", listen_port_);
}

void GuiTcpListener::stop() {
    if (!is_running_.exchange(false)) return;
    if (server_fd_ >= 0) {
        CLOSE_SOCK(server_fd_);
        server_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void GuiTcpListener::run_accept_loop() {
    while (is_running_.load()) {
        sockaddr_in client_addr{};
        socklen_t   addr_len = sizeof(client_addr);
        int client_fd = static_cast<int>(
            ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len));
        if (client_fd < 0) continue;

        char ip_buf[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::string remote_addr = std::string(ip_buf) + ":" +
                                  std::to_string(ntohs(client_addr.sin_port));

        std::thread(&GuiTcpListener::handle_client, this, client_fd, remote_addr).detach();
    }
}

void GuiTcpListener::handle_client(int client_fd, const std::string& remote_addr) {
    SessionManager::instance().register_session(client_fd, remote_addr);

    while (is_running_.load()) {
        std::string json_request;
        if (!recv_one_request(client_fd, json_request)) break;
        router_.route(client_fd, remote_addr, json_request);
    }

    SessionManager::instance().unregister_session(client_fd);
    CLOSE_SOCK(client_fd);
}

// ── 패킷 수신 ────────────────────────────────────────────────────────

static bool recv_n(int fd, void* buf, std::size_t n) {
    std::size_t total = 0;
    auto* p = static_cast<char*>(buf);
    while (total < n) {
        int got = static_cast<int>(::recv(fd, p + total, static_cast<int>(n - total), 0));
        if (got <= 0) return false;
        total += static_cast<std::size_t>(got);
    }
    return true;
}

bool GuiTcpListener::recv_one_request(int client_fd, std::string& out_json) {
    uint8_t header[HEADER_SIZE];
    if (!recv_n(client_fd, header, HEADER_SIZE)) return false;

    uint32_t json_size = (uint32_t)header[0] << 24 |
                         (uint32_t)header[1] << 16 |
                         (uint32_t)header[2] << 8  |
                         (uint32_t)header[3];

    if (json_size == 0 || json_size > 64 * 1024) return false;

    out_json.assign(json_size, '\0');
    if (!recv_n(client_fd, out_json.data(), json_size)) return false;
    return true;
}

} // namespace factory
