// HealthChecker.cpp
#include "monitor/health_checker.h"

#include <iostream>

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
                    // 복구
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
        // 종료 시 빠르게 탈출하기 위해 200ms 단위로 나누어 대기
        auto remaining = interval_;
        while (is_running_.load() && remaining.count() > 0) {
            auto step = std::min(remaining, std::chrono::seconds(1));
            std::this_thread::sleep_for(step);
            remaining -= step;
        }
    }
}

bool HealthChecker::ping_once(const HealthTarget& target) {
    int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(target.port);
    inet_pton(AF_INET, target.ip.c_str(), &addr.sin_addr);

    bool ok = (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    // TODO: 실제로는 connect 성공 후 heartbeat JSON 송신 → pong 수신 확인까지
    CLOSE_SOCK(fd);
    return ok;
}

} // namespace factory
