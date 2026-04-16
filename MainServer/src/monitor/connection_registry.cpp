// ConnectionRegistry.cpp
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

} // namespace factory
