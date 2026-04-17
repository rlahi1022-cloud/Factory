// ============================================================================
// health_checker.cpp — ConnectionRegistry 기반 헬스체크 구현
// ============================================================================
// ConnectionRegistry에 등록된 연결 수를 확인하여 AI서버 생존을 판정한다.
// AI서버가 메인서버(9000)에 접속하면 ConnectionRegistry에 자동 등록되므로,
// 별도 포트 ping 없이 연결 유무만으로 생존 확인이 가능하다.
// ============================================================================
#include "monitor/health_checker.h"
#include "monitor/connection_registry.h"
#include "core/logger.h"

#include <algorithm>

namespace factory {

HealthChecker::HealthChecker(EventBus& bus,
                             std::vector<HealthTarget> targets,
                             std::chrono::seconds interval)
    : event_bus_(bus),
      targets_(std::move(targets)),
      interval_(interval),
      is_running_(false) {
    // 초기 상태: 모든 서버를 장애(미연결)로 간주
    for (const auto& t : targets_) {
        down_state_map_[t.name] = true;
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
        // ConnectionRegistry에서 현재 연결된 서버 목록 가져오기
        auto connections = ConnectionRegistry::instance().get_all_connections();
        int connected_count = static_cast<int>(connections.size());

        for (const auto& target : targets_) {
            // target.ip를 포함하는 연결이 있는지 확인
            bool alive = false;
            for (const auto& [addr, fd] : connections) {
                if (addr.find(target.ip) != std::string::npos) {
                    alive = true;
                    break;
                }
            }

            if (alive) {
                if (down_state_map_[target.name]) {
                    // 장애 → 복구 전환
                    log_main("서버 복구 감지 | %s (%s)",
                             target.name.c_str(), target.ip.c_str());
                    ServerStatusEvent ev{target.name, target.ip, target.port};
                    event_bus_.publish(EventType::SERVER_RECOVERED, ev);
                    down_state_map_[target.name] = false;
                }
                log_main("서버 생존 확인 | %s (%s)",
                         target.name.c_str(), target.ip.c_str());
            } else {
                if (!down_state_map_[target.name]) {
                    // 정상 → 장애 전환
                    log_err_main("서버 장애 감지 | %s (%s)",
                                 target.name.c_str(), target.ip.c_str());
                    ServerStatusEvent ev{target.name, target.ip, target.port};
                    event_bus_.publish(EventType::SERVER_DOWN, ev);
                    down_state_map_[target.name] = true;
                } else {
                    log_err_main("서버 미연결 | %s (%s)",
                                 target.name.c_str(), target.ip.c_str());
                }
            }
        }

        log_main("연결 현황 | 총 %d개 AI서버 접속 중", connected_count);

        // interval_ 동안 1초 단위로 sleep (빠른 종료 지원)
        auto remaining = interval_;
        while (is_running_.load() && remaining.count() > 0) {
            auto step = std::min(remaining, std::chrono::seconds(1));
            std::this_thread::sleep_for(step);
            remaining -= step;
        }
    }
}

} // namespace factory
