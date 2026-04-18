// ============================================================================
// tcp_listener.cpp — TCP 수신 리스너 구현
// ============================================================================
// AI 추론/학습 서버로부터 검사 결과 패킷을 수신하여 EventBus에 발행한다.
//
// 패킷 프로토콜:
//   [4바이트 JSON 길이(Big-Endian)] + [JSON 본문] + [이미지 바이너리(옵션)]
//
// 크로스 플랫폼 참고:
//   본 구현은 POSIX 소켓 기준. Windows(MFC 환경)에서는
//   <winsock2.h> + WSAStartup/closesocket으로 치환 필요.
//   #ifdef _WIN32 분기로 소켓 닫기 매크로(CLOSE_SOCK)를 분리함.
// ============================================================================

#include "core/tcp_listener.h"
#include "monitor/connection_registry.h"
#include "security/ip_filter.h"
#include "Protocol.h"

#include "core/logger.h"

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
        log_err_main("소켓 생성 실패 | AI수신 리스너");
        is_running_.store(false);
        return;
    }

    // SO_REUSEADDR: 서버 재시작 시 TIME_WAIT 상태 포트 재사용 허용
    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    // accept()에 1초 타임아웃 설정 — stop() 호출 시 accept 블로킹에서 빠져나오기 위함
    struct timeval tv{1, 0};
    ::setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(listen_port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_err_main("바인드 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 16) < 0) {
        log_err_main("리슨 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    accept_thread_ = std::thread(&TcpListener::run_accept_loop, this);
    log_main("AI수신 리스너 시작 | 포트=%d", listen_port_);
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
            // 타임아웃 또는 종료 시 무시하고 루프 재확인
            continue;
        }
        char ip_buf[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::string client_ip(ip_buf);

        // IP 화이트리스트 검증 — 허용된 내부망 IP만 접속 가능
        if (!factory::security::is_allowed_ip(client_ip)) {
            log_err_main("비인가 IP 차단 | ip=%s", client_ip.c_str());
            CLOSE_SOCK(client_fd);
            continue;
        }

        // "IP:PORT" 형식으로 조합 — ACK 전송 시 ConnectionRegistry의 키로 사용
        std::string remote_addr = client_ip + ":" +
                                  std::to_string(ntohs(client_addr.sin_port));

        // 동시 접속 수 제한 (최대 10개 AI서버)
        int conn_count = static_cast<int>(
            ConnectionRegistry::instance().get_all_connections().size());
        if (conn_count >= 10) {
            log_err_main("AI서버 최대 접속 수 초과 | 현재=%d", conn_count);
            CLOSE_SOCK(client_fd);
            continue;
        }

        std::thread(&TcpListener::handle_client, this, client_fd, remote_addr).detach();
    }
}

void TcpListener::handle_client(int client_fd, const std::string& remote_addr) {
    // recv 타임아웃 1시간 — Training 서버 같은 유휴 연결 유지
    // (Slow Loris 방어는 max_connections 상한으로 대체)
    // AI 추론서버는 지속 통신, Training은 학습 주기 단위로 통신
    struct timeval client_tv{3600, 0};
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&client_tv), sizeof(client_tv));

    // TCP Keepalive — OS가 2시간 유휴 후 죽은 연결 자동 감지
    int keepalive = 1;
    ::setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE,
                 reinterpret_cast<const char*>(&keepalive), sizeof(keepalive));

    ConnectionRegistry::instance().register_connection(remote_addr, client_fd);
    log_main("AI서버 연결 | fd=%d ip=%s", client_fd, remote_addr.c_str());

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

    log_main("AI서버 연결 해제 | fd=%d ip=%s", client_fd, remote_addr.c_str());
    ConnectionRegistry::instance().unregister_connection(remote_addr);
    CLOSE_SOCK(client_fd);
}

// 정확히 n바이트를 수신할 때까지 반복 호출 (TCP 스트림 특성상 한 번에 안 올 수 있음)
// 연결 끊김 또는 에러 시 false 반환
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
        log_err_main("잘못된 JSON 크기 | size=%u", json_size);
        return false;
    }

    out_json.assign(json_size, '\0');
    if (!recv_n(client_fd, out_json.data(), json_size)) return false;

    // 이미지 바이너리 크기를 알아야 수신할 수 있으므로,
    // 전체 JSON 파싱 없이 "image_size" 키만 문자열 검색으로 추출한다.
    // (성능 최적화: nlohmann::json 파싱은 Router에서 한 번만 수행)
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
        // 50MB 상한 — 비정상 패킷으로 인한 메모리 폭주 방지
        if (image_size > 50 * 1024 * 1024) {
            log_err_main("이미지 크기 초과 | size=%zu", image_size);
            return false;
        }
        out_image.resize(image_size);
        if (!recv_n(client_fd, out_image.data(), image_size)) return false;
    }
    return true;
}

} // namespace factory
