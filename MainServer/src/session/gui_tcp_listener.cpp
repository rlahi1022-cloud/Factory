// gui_tcp_listener.cpp
// MFC GUI 클라이언트 전용 TCP 리스너
// 추론서버용 TcpListener와 동일한 패킷 포맷 사용 (4byte length BE + JSON)

#include "session/gui_tcp_listener.h"
#include "session/session_manager.h"
#include "Protocol.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using socklen_t = int;
  #define CLOSE_SOCK closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCK ::close
#endif

namespace factory {

GuiTcpListener::GuiTcpListener(EventBus& bus, uint16_t port)
    : event_bus_(bus),
      listen_port_(port),
      server_fd_(-1),
      is_running_(false) {
}

GuiTcpListener::~GuiTcpListener() {
    stop();
}

void GuiTcpListener::start() {
    if (is_running_.exchange(true)) return;

    server_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd_ < 0) {
        std::cerr << "[GuiTcpListener] socket() failed" << std::endl;
        is_running_.store(false);
        return;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(listen_port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[GuiTcpListener] bind() failed on port " << listen_port_ << std::endl;
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 8) < 0) {
        std::cerr << "[GuiTcpListener] listen() failed" << std::endl;
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    accept_thread_ = std::thread(&GuiTcpListener::run_accept_loop, this);
    std::cout << "[GuiTcpListener] listening on :" << listen_port_ << std::endl;
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
        if (client_fd < 0) {
            if (is_running_.load()) {
                std::cerr << "[GuiTcpListener] accept() failed" << std::endl;
            }
            continue;
        }
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
        if (!recv_one_request(client_fd, json_request)) {
            break;
        }
        // TODO: json_request에서 protocol_no를 파싱하여
        //       해당 이벤트를 EventBus에 발행
        //       예) LOGIN_REQ(100), INSPECT_HISTORY_REQ(114), STATS_REQ(130) 등
        std::cout << "[GuiTcpListener] request from " << remote_addr
                  << " : " << json_request.substr(0, 80) << std::endl;
    }

    SessionManager::instance().unregister_session(client_fd);
    CLOSE_SOCK(client_fd);
}

// 정확히 n바이트 수신 보장
static bool recv_n(int fd, void* buf, std::size_t n) {
    std::size_t total = 0;
    auto*       p     = static_cast<char*>(buf);
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

    if (json_size == 0 || json_size > 64 * 1024) {
        std::cerr << "[GuiTcpListener] invalid json size: " << json_size << std::endl;
        return false;
    }

    out_json.assign(json_size, '\0');
    if (!recv_n(client_fd, out_json.data(), json_size)) return false;
    return true;
}

} // namespace factory
