// TcpListener.cpp
// 주의: 본 구현은 POSIX 소켓 기준 골격임. Windows(MFC 환경)에서는
//       <winsock2.h> + WSAStartup/closesocket으로 치환 필요.
//       함수/변수 명명은 동일하게 유지.

#include "TcpListener.h"
#include "ConnectionRegistry.h"
#include "Protocol.h"

#include <iostream>
#include <cstring>

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

TcpListener::TcpListener(EventBus& bus, uint16_t port)
    : event_bus_(bus),
      listen_port_(port),
      server_fd_(-1),
      is_running_(false) {
}

TcpListener::~TcpListener() {
    stop();
}

void TcpListener::start() {
    if (is_running_.exchange(true)) return;

    server_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd_ < 0) {
        std::cerr << "[TcpListener] socket() failed" << std::endl;
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
        std::cerr << "[TcpListener] bind() failed on port " << listen_port_ << std::endl;
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 8) < 0) {
        std::cerr << "[TcpListener] listen() failed" << std::endl;
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    accept_thread_ = std::thread(&TcpListener::run_accept_loop, this);
    std::cout << "[TcpListener] listening on :" << listen_port_ << std::endl;
}

void TcpListener::stop() {
    if (!is_running_.exchange(false)) return;
    if (server_fd_ >= 0) {
        CLOSE_SOCK(server_fd_);
        server_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void TcpListener::run_accept_loop() {
    while (is_running_.load()) {
        sockaddr_in client_addr{};
        socklen_t   addr_len = sizeof(client_addr);
        int client_fd = static_cast<int>(
            ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len));
        if (client_fd < 0) {
            if (is_running_.load()) {
                std::cerr << "[TcpListener] accept() failed" << std::endl;
            }
            continue;
        }
        char ip_buf[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        // sender_addr 형식: "IP:PORT" (ACK 라우팅 키)
        std::string remote_addr = std::string(ip_buf) + ":" +
                                  std::to_string(ntohs(client_addr.sin_port));

        // 클라이언트당 1 스레드 (간단 구조). 추후 thread pool 가능.
        std::thread(&TcpListener::handle_client, this, client_fd, remote_addr).detach();
    }
}

void TcpListener::handle_client(int client_fd, const std::string& remote_addr) {
    ConnectionRegistry::instance().register_connection(remote_addr, client_fd);
    while (is_running_.load()) {
        std::string          json_payload;
        std::vector<uint8_t> image_bytes;
        if (!recv_one_packet(client_fd, json_payload, image_bytes)) {
            break;
        }

        PacketReceivedEvent ev{};
        ev.json_payload = std::move(json_payload);
        ev.image_bytes  = std::move(image_bytes);
        ev.remote_addr  = remote_addr;
        event_bus_.publish(EventType::PACKET_RECEIVED, ev);
    }
    ConnectionRegistry::instance().unregister_connection(remote_addr);
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

bool TcpListener::recv_one_packet(int client_fd,
                                  std::string& out_json,
                                  std::vector<uint8_t>& out_image) {
    // [4byte length(BE)] + [JSON] + [image?]
    uint8_t header[HEADER_SIZE];
    if (!recv_n(client_fd, header, HEADER_SIZE)) return false;

    uint32_t json_size = (uint32_t)header[0] << 24 |
                         (uint32_t)header[1] << 16 |
                         (uint32_t)header[2] << 8  |
                         (uint32_t)header[3];

    if (json_size == 0 || json_size > 64 * 1024) {
        std::cerr << "[TcpListener] invalid json size: " << json_size << std::endl;
        return false;
    }

    out_json.assign(json_size, '\0');
    if (!recv_n(client_fd, out_json.data(), json_size)) return false;

    // image_size 필드는 라우터/핸들러에서 JSON 파싱 후 알 수 있으므로
    // 여기서는 가벼운 파싱 — "image_size":NNN 만 정규식 없이 추출
    std::size_t image_size = 0;
    auto pos = out_json.find("\"image_size\"");
    if (pos != std::string::npos) {
        auto colon = out_json.find(':', pos);
        if (colon != std::string::npos) {
            image_size = static_cast<std::size_t>(std::strtoul(
                out_json.c_str() + colon + 1, nullptr, 10));
        }
    }

    if (image_size > 0) {
        if (image_size > 50 * 1024 * 1024) {
            std::cerr << "[TcpListener] image too large: " << image_size << std::endl;
            return false;
        }
        out_image.resize(image_size);
        if (!recv_n(client_fd, out_image.data(), image_size)) return false;
    }
    return true;
}

} // namespace factory
