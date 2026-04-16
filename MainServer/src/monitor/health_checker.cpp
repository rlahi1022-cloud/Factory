// health_checker.cpp
// 5초 주기 HEALTH_PING(1200) 송신 → HEALTH_PONG(1201) 수신 확인
#include "monitor/health_checker.h"
#include "Protocol.h"

#include <cstring>
#include <iostream>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define CLOSE_SOCK closesocket
#else
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCK ::close
#endif

namespace factory {

HealthChecker::HealthChecker(EventBus& bus,
                             std::vector<HealthTarget> targets,
                             std::chrono::seconds interval,
                             int fail_threshold)
    : event_bus_(bus),
      targets_(std::move(targets)),
      interval_(interval),
      fail_threshold_(fail_threshold),
      is_running_(false) {
    for (const auto& t : targets_) {
        fail_count_map_[t.name] = 0;
        down_state_map_[t.name] = false;
    }
}

HealthChecker::~HealthChecker() {
    stop();
}

void HealthChecker::start() {
    if (is_running_.exchange(true)) return;
    worker_thread_ = std::thread(&HealthChecker::run_loop, this);
}

void HealthChecker::stop() {
    if (!is_running_.exchange(false)) return;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void HealthChecker::run_loop() {
    while (is_running_.load()) {
        event_bus_.publish(EventType::HEALTH_CHECK_TICK, std::any{});

        for (const auto& target : targets_) {
            bool alive = ping_once(target);
            if (alive) {
                if (down_state_map_[target.name]) {
                    ServerStatusEvent ev{target.name, target.ip, target.port};
                    event_bus_.publish(EventType::SERVER_RECOVERED, ev);
                    down_state_map_[target.name] = false;
                }
                fail_count_map_[target.name] = 0;
            } else {
                fail_count_map_[target.name]++;
                if (fail_count_map_[target.name] >= fail_threshold_ &&
                    !down_state_map_[target.name]) {
                    ServerStatusEvent ev{target.name, target.ip, target.port};
                    event_bus_.publish(EventType::SERVER_DOWN, ev);
                    down_state_map_[target.name] = true;
                }
            }
        }
        // 종료 시 빠르게 탈출하기 위해 1초 단위로 대기
        auto remaining = interval_;
        while (is_running_.load() && remaining.count() > 0) {
            auto step = std::min(remaining, std::chrono::seconds(1));
            std::this_thread::sleep_for(step);
            remaining -= step;
        }
    }
}

// 정확히 n바이트 수신
static bool recv_n_timeout(int fd, void* buf, std::size_t n) {
    std::size_t total = 0;
    auto* p = static_cast<char*>(buf);
    while (total < n) {
        int got = static_cast<int>(::recv(fd, p + total, static_cast<int>(n - total), 0));
        if (got <= 0) return false;
        total += static_cast<std::size_t>(got);
    }
    return true;
}

bool HealthChecker::ping_once(const HealthTarget& target) {
    int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return false;

    // 연결 타임아웃 2초
    struct timeval tv{2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(target.port);
    inet_pton(AF_INET, target.ip.c_str(), &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        CLOSE_SOCK(fd);
        return false;
    }

    // HEALTH_PING(1200) JSON 패킷 송신
    std::ostringstream os;
    os << "{\"protocol_no\":1200"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"server_name\":\"main_server\""
       << "}";
    std::string json_body = os.str();

    uint32_t json_size = static_cast<uint32_t>(json_body.size());
    uint8_t header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),
        static_cast<uint8_t>((json_size >> 16) & 0xFF),
        static_cast<uint8_t>((json_size >>  8) & 0xFF),
        static_cast<uint8_t>( json_size        & 0xFF),
    };

    if (::send(fd, reinterpret_cast<const char*>(header), 4, 0) != 4 ||
        ::send(fd, json_body.c_str(), static_cast<int>(json_body.size()), 0) !=
            static_cast<int>(json_body.size())) {
        CLOSE_SOCK(fd);
        return false;
    }

    // HEALTH_PONG(1201) 수신 대기 (2초 타임아웃)
    uint8_t resp_header[4];
    if (!recv_n_timeout(fd, resp_header, 4)) {
        CLOSE_SOCK(fd);
        return false;
    }

    uint32_t resp_size = (uint32_t)resp_header[0] << 24 |
                         (uint32_t)resp_header[1] << 16 |
                         (uint32_t)resp_header[2] << 8  |
                         (uint32_t)resp_header[3];

    if (resp_size == 0 || resp_size > 4096) {
        CLOSE_SOCK(fd);
        return false;
    }

    std::string resp_json(resp_size, '\0');
    if (!recv_n_timeout(fd, resp_json.data(), resp_size)) {
        CLOSE_SOCK(fd);
        return false;
    }

    CLOSE_SOCK(fd);

    // protocol_no가 1201(HEALTH_PONG)인지 확인
    auto pos = resp_json.find("\"protocol_no\"");
    if (pos != std::string::npos) {
        auto colon = resp_json.find(':', pos);
        if (colon != std::string::npos) {
            int pno = static_cast<int>(std::strtol(resp_json.c_str() + colon + 1, nullptr, 10));
            return (pno == static_cast<int>(ProtocolNo::HEALTH_PONG));
        }
    }
    return false;
}

} // namespace factory
