// ============================================================================
// connection_registry.cpp — 하위 서버 연결 fd 레지스트리 구현
// ============================================================================
// sender_addr(IP:PORT 문자열)을 키로 fd를 저장/조회/삭제한다.
// ACK 전송 시 원래 연결을 찾기 위한 단순 룩업 테이블이다.
// ============================================================================
#include "monitor/connection_registry.h"

namespace factory {

ConnectionRegistry& ConnectionRegistry::instance() {
    static ConnectionRegistry registry;
    return registry;
}

void ConnectionRegistry::register_connection(const std::string& sender_addr, int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    fd_map_[sender_addr] = client_fd;
}

void ConnectionRegistry::unregister_connection(const std::string& sender_addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    fd_map_.erase(sender_addr);
}

int ConnectionRegistry::find_fd(const std::string& sender_addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fd_map_.find(sender_addr);
    return (it != fd_map_.end()) ? it->second : -1;
}

std::unordered_map<std::string, int> ConnectionRegistry::get_all_connections() {
    std::lock_guard<std::mutex> lock(mutex_);
    return fd_map_;
}

} // namespace factory
