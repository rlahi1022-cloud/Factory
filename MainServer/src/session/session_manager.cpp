// ============================================================================
// session_manager.cpp — GUI 클라이언트 세션 관리자 구현
// ============================================================================
// 싱글톤 패턴으로 전역 세션 맵을 관리하며, 모든 접근은 mutex로 보호한다.
// 전송 프로토콜: [4바이트 빅엔디안 길이] + [JSON 본문]
// ============================================================================
#include "session/session_manager.h"

#include "core/logger.h"

#include <cstdint>
#include <cstring>
#include <iostream>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <sys/socket.h>
#endif

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

void SessionManager::broadcast(const std::string& json_message, int station_filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [fd, session] : sessions_) {
        // 필터링 규칙:
        //   station_filter==0 → 모든 클라이언트에 전송
        //   station_filter!=0 → 해당 station 구독자 + 전체 구독자(subscribed_station==0)에만 전송
        if (station_filter != 0 &&
            session.subscribed_station != 0 &&
            session.subscribed_station != station_filter) {
            continue;
        }
        if (!send_json(fd, json_message)) {
            log_err_push("브로드캐스트 실패 | fd=%d ip=%s", fd,
                         session.remote_addr.c_str());
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

bool SessionManager::send_json(int fd, const std::string& json_body) {
    // MFC 클라이언트와의 프레이밍 프로토콜: [4byte 빅엔디안 길이] + [JSON 본문]
    // MFC 측에서도 동일한 헤더를 파싱하여 JSON 경계를 식별한다.
    uint32_t json_size = static_cast<uint32_t>(json_body.size());
    uint8_t header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),
        static_cast<uint8_t>((json_size >> 16) & 0xFF),
        static_cast<uint8_t>((json_size >>  8) & 0xFF),
        static_cast<uint8_t>( json_size        & 0xFF),
    };

    int sent_h = static_cast<int>(::send(fd, reinterpret_cast<const char*>(header), 4, 0));
    int sent_b = static_cast<int>(::send(fd, json_body.c_str(),
                                         static_cast<int>(json_body.size()), 0));
    return (sent_h == 4 && sent_b == static_cast<int>(json_body.size()));
}

} // namespace factory
