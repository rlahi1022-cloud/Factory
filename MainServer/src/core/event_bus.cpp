// ============================================================================
// event_bus.cpp — 워커 풀 기반 EventBus 구현
// ============================================================================
// N개 워커 스레드가 큐에서 이벤트를 경쟁적으로 꺼내 병렬 처리한다.
// publish()는 큐에 적재만 하고 즉시 반환(논블로킹).
// ============================================================================

#include "core/event_bus.h"
#include "core/logger.h"

namespace factory {

EventBus::EventBus(int worker_count)
    : worker_count_(worker_count),
      is_running_(false) {
}

EventBus::~EventBus() {
    stop();
}

void EventBus::start() {
    if (is_running_.exchange(true)) return;

    // 워커 풀 생성
    workers_.reserve(worker_count_);
    for (int i = 0; i < worker_count_; ++i) {
        workers_.emplace_back(&EventBus::worker_loop, this);
    }
    log_main("EventBus 시작 | 워커=%d개", worker_count_);
}

void EventBus::stop() {
    if (!is_running_.exchange(false)) return;

    // 모든 워커를 깨워서 종료 조건 확인시킴
    queue_cv_.notify_all();

    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
    log_main("EventBus 종료 | 워커 %d개 정리 완료", worker_count_);
}

void EventBus::subscribe(EventType type, Handler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handler_map_[type].push_back(std::move(handler));
}

void EventBus::publish(EventType type, std::any payload) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        event_queue_.push(Event{type, std::move(payload)});
    }
    queue_cv_.notify_one();  // 대기 중인 워커 1개를 깨움
}

void EventBus::worker_loop() {
    while (true) {
        Event event;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !event_queue_.empty() || !is_running_.load();
            });

            // 종료 신호 + 큐 비어있으면 탈출
            if (!is_running_.load() && event_queue_.empty()) break;
            if (event_queue_.empty()) continue;

            event = std::move(event_queue_.front());
            event_queue_.pop();
        }

        // 핸들러 스냅샷 복사 → 락 해제 후 실행
        std::vector<Handler> handlers_snapshot;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            auto it = handler_map_.find(event.type);
            if (it != handler_map_.end()) {
                handlers_snapshot = it->second;
            }
        }

        // 같은 이벤트의 핸들러들은 순차 호출, 개별 예외 격리
        for (auto& handler : handlers_snapshot) {
            try {
                handler(event.payload);
            } catch (const std::exception& e) {
                log_err_main("이벤트 핸들러 예외 | %s", e.what());
            } catch (...) {
                log_err_main("이벤트 핸들러 알수없는 예외");
            }
        }
    }
}

} // namespace factory
