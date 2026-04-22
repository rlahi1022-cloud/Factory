// ============================================================================
// session_manager.cpp — GUI 클라이언트 세션 관리자 구현
// ============================================================================
// 싱글톤 패턴으로 전역 세션 맵을 관리하며, 모든 접근은 mutex로 보호한다.
// 전송 프로토콜: [4바이트 빅엔디안 길이] + [JSON 본문]
// ============================================================================
#include "session/session_manager.h"

#include "core/logger.h"
#include "core/tcp_utils.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace factory {

SessionManager& SessionManager::instance() {
    static SessionManager mgr;
    return mgr;
}

void SessionManager::register_session(int client_fd, const std::string& remote_addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    GuiSession session{};
    session.client_fd   = client_fd;
    session.remote_addr = remote_addr;
    sessions_[client_fd] = session;
    log_clt("클라이언트 접속 | fd=%d ip=%s | 현재접속=%zu", client_fd,
            remote_addr.c_str(), sessions_.size());
}

void SessionManager::unregister_session(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        log_clt("클라이언트 해제 | fd=%d ip=%s", client_fd,
                it->second.remote_addr.c_str());
        sessions_.erase(it);
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

// 대상 세션 스냅샷 구조체 (mutex 해제 후 전송에 사용)
// fd 하나와 주소 문자열만 보관 → GuiSession 전체를 복사하지 않아 메모리 절약
struct BroadcastTarget {
    int         fd;              // 소켓 파일 디스크립터
    std::string remote_addr;     // 로그용 "IP:PORT" 문자열
};

// 필터링된 수신 대상 fd 목록을 mutex 보호 하에 "스냅샷" 복사한다.
// 이후 실제 send()는 mutex 밖에서 수행 → 느린 클라이언트가 다른 세션에 영향을 주지 않음.
// (이전 버그: mutex 보유한 채로 send() 호출 → 느린 클라 1명이 전체 마비)
static std::vector<BroadcastTarget> snapshot_targets(
    std::unordered_map<int, GuiSession>& sessions,
    int station_filter)
{
    std::vector<BroadcastTarget> targets;
    targets.reserve(sessions.size());   // 최대 세션 수만큼 미리 할당 (재할당 방지)

    // 구조적 바인딩(C++17): pair의 first/second를 fd/session으로 분해
    for (const auto& [fd, session] : sessions) {
        // 필터링 규칙:
        //   station_filter==0 → 모든 클라이언트에 전송 (전역 알림)
        //   station_filter!=0 → 해당 station 구독자 + 전체 구독자(subscribed_station==0)에만 전송
        if (station_filter != 0 &&                              // 필터 활성화됐고
            session.subscribed_station != 0 &&                  // 사용자가 "전체 보기" 아니고
            session.subscribed_station != station_filter) {     // 구독 station이 다르면
            continue;                                           // → 건너뛰기
        }
        // 대상에 포함 (초기화 리스트 문법: {fd, remote_addr})
        targets.push_back({fd, session.remote_addr});
    }
    return targets;
}

void SessionManager::broadcast(const std::string& json_message, int station_filter) {
    // 1) mutex 짧게 잡고 수신자 fd 목록만 스냅샷 (< 1ms)
    std::vector<BroadcastTarget> targets;
    {
        // 중괄호 스코프로 lock_guard 수명을 제한
        // 이 블록 끝나면 자동 unlock → 다른 스레드의 register/login 대기 시간 최소
        std::lock_guard<std::mutex> lock(mutex_);
        targets = snapshot_targets(sessions_, station_filter);
    }   // ← 여기서 mutex 해제됨

    // 2) mutex 해제 후 전송 → 한 클라이언트가 느려도 다른 세션 블로킹 없음
    //    한 클라 send()가 30초 블록되도 다른 세션 접속/로그인은 즉시 처리 가능
    for (const auto& t : targets) {
        if (!send_json(t.fd, json_message)) {           // 실패 시 로그만 남기고 계속 진행
            log_err_push("브로드캐스트 실패 | fd=%d ip=%s", t.fd,
                         t.remote_addr.c_str());
        }
    }
}

void SessionManager::broadcast_with_binary(const std::string& json_message,
                                            const std::vector<uint8_t>& binary_data,
                                            int station_filter) {
    // 동일 스냅샷 패턴: fd 목록만 복사 후 mutex 해제
    std::vector<BroadcastTarget> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        targets = snapshot_targets(sessions_, station_filter);
    }
    for (const auto& t : targets) {
        // [4바이트 헤더] + [JSON] + [이미지 바이너리]
        if (!send_json(t.fd, json_message)) {
            log_err_push("브로드캐스트 실패 | fd=%d ip=%s", t.fd,
                         t.remote_addr.c_str());
            continue;
        }
        if (!binary_data.empty()) {
            if (!send_all(t.fd, binary_data.data(), binary_data.size())) {
                log_err_push("이미지 전송 실패 | fd=%d size=%zu", t.fd, binary_data.size());
            }
        }
    }
}

bool SessionManager::send_to(int client_fd, const std::string& json_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it == sessions_.end()) return false;
    return send_json(client_fd, json_message);
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
    // 소켓 강제 종료 → handle_client의 recv가 실패하며 자연스럽게 unregister됨
    ::shutdown(client_fd, SHUT_RDWR);
    log_clt("세션 강제 종료 | fd=%d (중복 로그인)", client_fd);
}

bool SessionManager::send_json(int fd, const std::string& json_body) {
    return send_json_frame(fd, json_body);
}

} // namespace factory
