#pragma once
// ConnectionRegistry.h
// TcpListener가 수락한 클라이언트 연결을 sender_addr 키로 보관.
// ACK 송신 시 같은 connection으로 회신하기 위해 사용.

#include <mutex>
#include <string>
#include <unordered_map>

namespace factory {

class ConnectionRegistry {
public:
    static ConnectionRegistry& instance();

    void register_connection(const std::string& sender_addr, int client_fd);
    void unregister_connection(const std::string& sender_addr);

    // 없으면 -1 반환
    int  find_fd(const std::string& sender_addr);

private:
    ConnectionRegistry() = default;
    std::mutex                                mutex_;
    std::unordered_map<std::string, int>      fd_map_;
};

} // namespace factory
