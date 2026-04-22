// ============================================================================
// session_manager.cpp — GUI 세션 관리 + 클라이언트별 송신 큐 (v0.14.3)
// ============================================================================
// 핵심 변경 (v0.14.3):
//   각 GuiSession 마다 전용 송신 큐(deque) + 전용 스레드 를 둔다.
//   broadcast 는 큐에 push 하고 즉시 반환 → 한 느린 클라가 다른 세션의 송신을
//   블록하지 않음. 큐 초과 시 가장 오래된 항목 드랍(drop-oldest).
//
// 이유:
//   NG_PUSH 3장(5~6MB) 를 직렬 send 하면 한 명이 느려도 전체 블록 →
//   keepalive/SNDTIMEO 로 줄줄이 끊김 발생. 클라별 독립 스레드로 격리.
//
// 와이어 포맷:
//   [4바이트 Big-Endian JSON 길이] + [JSON 본문] (+ [바이너리 페이로드])
// ============================================================================
#include "session/session_manager.h"

#include "core/logger.h"
#include "core/tcp_utils.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>

namespace factory {

SessionManager& SessionManager::instance() {
    static SessionManager mgr;
    return mgr;
}

// ---------------------------------------------------------------------------
// register_session — 새 연결 → 송신 큐/스레드도 같이 생성
// ---------------------------------------------------------------------------
void SessionManager::register_session(int client_fd, const std::string& remote_addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& sess = sessions_[client_fd];
    sess.client_fd   = client_fd;
    sess.remote_addr = remote_addr;

    // v0.14.3: 송신 큐/동기화 객체/스레드 초기화
    sess.queue       = std::make_unique<std::deque<OutgoingMessage>>();
    sess.queue_mutex = std::make_unique<std::mutex>();
    sess.queue_cv    = std::make_unique<std::condition_variable>();
    sess.running     = std::make_unique<std::atomic<bool>>(true);
    sess.sender      = std::make_unique<std::thread>(&SessionManager::sender_loop, &sess);

    log_clt("클라이언트 접속 | fd=%d ip=%s | 현재접속=%zu", client_fd,
            remote_addr.c_str(), sessions_.size());
}

// ---------------------------------------------------------------------------
// unregister_session — 세션 제거 + 송신 스레드 정리
//
// v0.14.3.1 버그 수정:
//   이전 구현은 erase(it) 를 먼저 하고 나중에 sender->join() 했는데,
//   erase 로 GuiSession 이 소멸되면 sender_loop 이 계속 접근하는 running/queue/cv
//   같은 unique_ptr 들도 같이 소멸 → **use-after-free → segfault**.
//
//   수정: (1) 스레드에 종료 신호만 보내고 thread object 만 꺼냄
//         (2) **mutex 밖에서** sender 스레드가 완전히 종료될 때까지 join
//         (3) 스레드 종료 확정된 뒤 erase
//   순서가 중요 — 스레드가 sess 포인터 참조를 멈춘 뒤에만 erase 해야 안전.
//
// 다른 쓰레드와의 레이스:
//   (1) 단계 이후 broadcast 가 들어오면 큐에 쌓이지만 sender 가 곧 종료되어
//   소비되지 않음 → 큐에만 쌓임. (3) 에서 erase 시 자연스럽게 소멸.
//   메모리 약간 낭비되지만 crash 보다 낫다.
// ---------------------------------------------------------------------------
void SessionManager::unregister_session(int client_fd) {
    // (1) 스레드에 종료 신호만 보내고 thread object 획득
    std::unique_ptr<std::thread> sender_to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(client_fd);
        if (it == sessions_.end()) return;
        if (it->second.running)  it->second.running->store(false);
        if (it->second.queue_cv) it->second.queue_cv->notify_all();
        sender_to_join = std::move(it->second.sender);
        // erase 는 아직 하지 않음 — 스레드가 아직 running/queue 참조 중일 수 있음
    }

    // (2) mutex 밖에서 스레드가 루프 탈출 후 완전히 종료될 때까지 대기
    //     sender_loop 의 wait() 는 running==false 로 깨어나 즉시 리턴함.
    if (sender_to_join && sender_to_join->joinable()) {
        sender_to_join->join();
    }

    // (3) 스레드 종료 확정 → 안전하게 세션 삭제
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(client_fd);
        if (it != sessions_.end()) {
            log_clt("클라이언트 해제 | fd=%d ip=%s", client_fd,
                    it->second.remote_addr.c_str());
            sessions_.erase(it);
        }
    }
}

void SessionManager::set_client_info(int client_fd,
                                     const std::string& client_name,
                                     int subscribed_station) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        it->second.client_name       = client_name;
        it->second.subscribed_station = subscribed_station;
        log_clt("사용자 등록 | fd=%d 아이디=%s 스테이션=%d", client_fd,
                client_name.c_str(), subscribed_station);
    }
}

// ---------------------------------------------------------------------------
// enqueue_locked — 특정 세션의 큐에 메시지 추가 (내부 호출, 세션 mutex 잡은 채)
// 큐 상한 초과 시 가장 오래된(맨 앞) 항목 드랍.
// 실시간 NG 푸시는 최신이 중요하므로 drop-oldest 가 적절.
// ---------------------------------------------------------------------------
void SessionManager::enqueue_locked(GuiSession& sess, OutgoingMessage msg) {
    if (!sess.queue || !sess.queue_mutex || !sess.queue_cv) return;
    std::lock_guard<std::mutex> qlock(*sess.queue_mutex);
    if (sess.queue->size() >= MAX_QUEUE_SIZE) {
        sess.queue->pop_front();  // drop-oldest
        log_err_push("송신 큐 포화 — 오래된 항목 드랍 | fd=%d ip=%s",
                     sess.client_fd, sess.remote_addr.c_str());
    }
    sess.queue->push_back(std::move(msg));
    sess.queue_cv->notify_one();
}

// ---------------------------------------------------------------------------
// broadcast / broadcast_with_binary — 큐에 push 후 즉시 반환
// ---------------------------------------------------------------------------
void SessionManager::broadcast(const std::string& json_message, int station_filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [fd, sess] : sessions_) {
        if (station_filter != 0 &&
            sess.subscribed_station != 0 &&
            sess.subscribed_station != station_filter) {
            continue;
        }
        OutgoingMessage m;
        m.json = json_message;
        enqueue_locked(sess, std::move(m));
    }
}

void SessionManager::broadcast_with_binary(const std::string& json_message,
                                            const std::vector<uint8_t>& binary_data,
                                            int station_filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [fd, sess] : sessions_) {
        if (station_filter != 0 &&
            sess.subscribed_station != 0 &&
            sess.subscribed_station != station_filter) {
            continue;
        }
        OutgoingMessage m;
        m.json   = json_message;
        m.binary = binary_data;     // 복사 — 각 클라 큐가 독립된 복사본 보유
        enqueue_locked(sess, std::move(m));
    }
}

bool SessionManager::send_to(int client_fd, const std::string& json_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it == sessions_.end()) return false;
    OutgoingMessage m;
    m.json = json_message;
    enqueue_locked(it->second, std::move(m));
    return true;
}

std::size_t SessionManager::session_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

int SessionManager::find_fd_by_username(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [fd, session] : sessions_) {
        if (session.client_name == username) return fd;
    }
    return -1;
}

std::string SessionManager::get_remote_addr(int client_fd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    return (it != sessions_.end()) ? it->second.remote_addr : std::string{};
}

void SessionManager::force_close(int client_fd) {
    ::shutdown(client_fd, SHUT_RDWR);
    log_clt("세션 강제 종료 | fd=%d (중복 로그인)", client_fd);
}

// ---------------------------------------------------------------------------
// send_frame — 실제 TCP 송신 ([4B BE length][JSON][optional binary])
//   send_json_frame + send_all 조합과 동일 프로토콜. partial send 자동 재시도 포함.
// ---------------------------------------------------------------------------
bool SessionManager::send_frame(int fd, const OutgoingMessage& msg) {
    if (!send_json_frame(fd, msg.json)) return false;
    if (!msg.binary.empty()) {
        if (!send_all(fd, msg.binary.data(), msg.binary.size())) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// sender_loop — 세션별 송신 워커 스레드
//   queue_cv 로 깨어날 때까지 대기 → 한 건씩 꺼내 socket 에 write.
//   send 가 실패해도 연결을 끊지 않음 (recv 쪽에서 감지). 단, 너무 많이 실패하면
//   해당 클라의 큐만 쌓임 → drop-oldest 로 자연스럽게 해소.
// ---------------------------------------------------------------------------
void SessionManager::sender_loop(GuiSession* sess) {
    // 방어 — 구성 요소 중 하나라도 null 이면 즉시 종료 (등록 경로 버그 대비)
    if (!sess || !sess->running || !sess->queue ||
        !sess->queue_mutex || !sess->queue_cv) {
        return;
    }
    try {
        while (sess->running->load()) {
            OutgoingMessage msg;
            {
                std::unique_lock<std::mutex> qlock(*sess->queue_mutex);
                sess->queue_cv->wait(qlock, [sess] {
                    return !sess->queue->empty() || !sess->running->load();
                });
                if (!sess->running->load() && sess->queue->empty()) break;
                if (sess->queue->empty()) continue;  // spurious wakeup
                msg = std::move(sess->queue->front());
                sess->queue->pop_front();
            }
            // 소켓 fd 는 세션 수명동안 유효. 송신 실패는 log 만 남기고 다음 메시지로.
            if (!send_frame(sess->client_fd, msg)) {
                log_err_push("송신 실패 | fd=%d ip=%s json=%zu bin=%zu",
                             sess->client_fd, sess->remote_addr.c_str(),
                             msg.json.size(), msg.binary.size());
                // 연속 실패여도 계속 시도 (recv 쪽에서 dead 감지되면 세션 정리됨)
            }
        }
    } catch (const std::exception& exc) {
        log_err_push("sender_loop 예외 | fd=%d err=%s",
                     sess ? sess->client_fd : -1, exc.what());
    } catch (...) {
        log_err_push("sender_loop 알 수 없는 예외 | fd=%d",
                     sess ? sess->client_fd : -1);
    }
}

} // namespace factory
