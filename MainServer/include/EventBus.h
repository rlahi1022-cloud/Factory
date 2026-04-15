#pragma once
// EventBus.h
// 자체 구현 이벤트 버스 (std::function 기반)
// - publish/subscribe 패턴
// - thread-safe (mutex 보호)
// - 이벤트 처리는 내부 worker thread에서 수행 (비동기)
// - 페이로드는 std::any로 타입 소거 후 핸들러에서 any_cast로 복원

#include "EventTypes.h"

#include <any>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace factory {

class EventBus {
public:
    // 이벤트 핸들러 시그니처: 이벤트 타입에 맞는 페이로드를 std::any로 받음
    using Handler = std::function<void(const std::any&)>;

    EventBus();
    ~EventBus();

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    // 워커 스레드 시작 / 종료
    void start();
    void stop();

    // 핸들러 구독
    // 동일 이벤트에 여러 핸들러 등록 가능 (등록 순서대로 호출)
    void subscribe(EventType type, Handler handler);

    // 이벤트 발행 (내부 큐에 enqueue 후 즉시 반환)
    void publish(EventType type, std::any payload);

private:
    void run_worker_loop();

    struct Event {
        EventType type;
        std::any  payload;
    };

    std::unordered_map<EventType, std::vector<Handler>> handler_map_;
    std::mutex                                          handler_mutex_;

    std::queue<Event>                                   event_queue_;
    std::mutex                                          queue_mutex_;
    std::condition_variable                             queue_cv_;

    std::thread                                         worker_thread_;
    std::atomic<bool>                                   is_running_;
};

} // namespace factory
