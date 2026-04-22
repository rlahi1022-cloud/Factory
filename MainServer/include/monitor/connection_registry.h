// ============================================================================
// connection_registry.h — 클라이언트 연결(fd) 레지스트리
// ============================================================================
// 목적:
//   TcpListener가 수락한 하위 서버(추론/학습) 연결의 fd를 sender_addr 키로 보관한다.
//   ACK 응답을 보낼 때 동일 TCP 연결을 재사용하기 위해 존재한다.
//
// 참고:
//   GUI 클라이언트 세션은 SessionManager가 별도로 관리한다.
//   이 레지스트리는 서버 간(MainServer ↔ 추론/학습 서버) 연결 전용이다.
// ============================================================================
#pragma once

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

    // 연결된 모든 서버 목록 반환 (MODEL_RELOAD 브로드캐스트용)
    std::unordered_map<std::string, int> get_all_connections();

private:
    ConnectionRegistry() = default;
    std::mutex                                mutex_;
    std::unordered_map<std::string, int>      fd_map_;
};

} // namespace factory
