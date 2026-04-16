// EventBus.cpp
#include "event_bus.h"

#include <iostream>

namespace factory {

EventBus::EventBus()
    : is_running_(false) {
}

EventBus::~EventBus() {
    stop();
}

void EventBus::start() {
    if (is_running_.exchange(true)) {
        return; // 이미 실행 중
    }
    worker_thread_ = std::thread(&EventBus::run_worker_loop, this);
}

void EventBus::stop() {
    if (!is_running_.exchange(false)) {
        return;
    }
    queue_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
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
    queue_cv_.notify_one();
}

void EventBus::run_worker_loop() {
    while (is_running_.load()) {
        Event event;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !event_queue_.empty() || !is_running_.load();
            });
            if (!is_running_.load() && event_queue_.empty()) {
                break;
            }
            event = std::move(event_queue_.front());
            event_queue_.pop();
        }

        // 핸들러 스냅샷 (락 길게 잡지 않음)
        std::vector<Handler> handlers_snapshot;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            auto it = handler_map_.find(event.type);
            if (it != handler_map_.end()) {
                handlers_snapshot = it->second;
            }
        }

        for (auto& handler : handlers_snapshot) {
            try {
                handler(event.payload);
            } catch (const std::exception& e) {
                std::cerr << "[EventBus] handler exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[EventBus] handler unknown exception" << std::endl;
            }
        }
    }
}

} // namespace factory
