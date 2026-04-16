#pragma once
// HealthChecker.h
// 5초 주기로 학습/추론 서버에 TCP heartbeat ping 송신, pong 수신 확인.
// 3회 연속 무응답 시 SERVER_DOWN 이벤트 발행, 복구 시 SERVER_RECOVERED 발행.

#include "core/event_bus.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace factory {

struct HealthTarget {
    std::string name;
    std::string ip;
    uint16_t    port = 0;
};

class HealthChecker {
public:
    HealthChecker(EventBus& bus,
                  std::vector<HealthTarget> targets,
                  std::chrono::seconds interval = std::chrono::seconds(5),
                  int fail_threshold            = 3);
    ~HealthChecker();

    void start();
    void stop();

private:
    void run_loop();
    bool ping_once(const HealthTarget& target);

    EventBus&                       event_bus_;
    std::vector<HealthTarget>       targets_;
    std::chrono::seconds            interval_;
    int                             fail_threshold_;
    std::unordered_map<std::string, int> fail_count_map_;
    std::unordered_map<std::string, bool> down_state_map_;

    std::thread                     worker_thread_;
    std::atomic<bool>               is_running_;
};

} // namespace factory
