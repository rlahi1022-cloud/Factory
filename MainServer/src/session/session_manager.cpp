// session_manager.cpp
#include "session/session_manager.h"

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
    std::cout << "[SessionManager] session registered fd=" << client_fd
              << " addr=" << remote_addr
              << " (total=" << sessions_.size() << ")" << std::endl;
}

void SessionManager::unregister_session(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        std::cout << "[SessionManager] session removed fd=" << client_fd
                  << " addr=" << it->second.remote_addr << std::endl;
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
        std::cout << "[SessionManager] client info set fd=" << client_fd
                  << " name=" << client_name
                  << " station=" << subscribed_station << std::endl;
    }
}

void SessionManager::broadcast(const std::string& json_message, int station_filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [fd, session] : sessions_) {
        // station_filter가 0이면 전체, 아니면 해당 station 구독자 + 전체 구독자(0)만
        if (station_filter != 0 &&
            session.subscribed_station != 0 &&
            session.subscribed_station != station_filter) {
            continue;
        }
        if (!send_json(fd, json_message)) {
            std::cerr << "[SessionManager] broadcast failed fd=" << fd
                      << " addr=" << session.remote_addr << std::endl;
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
    // [4byte length BE] + [JSON]
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
