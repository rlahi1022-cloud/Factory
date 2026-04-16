#pragma once
// session_manager.h
// GUI 클라이언트 세션 관리
// - 접속한 MFC 클라이언트를 세션 단위로 등록/해제
// - 연결된 모든(또는 필터링된) 클라이언트에 JSON broadcast
// - GuiNotifier가 이벤트 수신 → SessionManager를 통해 실제 전송

#include "core/event_bus.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace factory {

// 개별 GUI 클라이언트 세션 정보
struct GuiSession {
    int         client_fd    = -1;
    std::string remote_addr;          // "IP:PORT"
    std::string client_name;          // 클라이언트 식별 (예: "operator_1")
    int         subscribed_station = 0; // 0이면 전체 구독, 1/2면 해당 station만
};

class SessionManager {
public:
    static SessionManager& instance();

    // 세션 등록/해제
    void register_session(int client_fd, const std::string& remote_addr);
    void unregister_session(int client_fd);

    // 클라이언트 이름/구독 station 설정 (로그인 후)
    void set_client_info(int client_fd,
                         const std::string& client_name,
                         int subscribed_station);

    // 연결된 모든 클라이언트에 JSON broadcast
    // station_filter: 0이면 전체, 1/2이면 해당 station 구독자만
    void broadcast(const std::string& json_message, int station_filter = 0);

    // 특정 클라이언트에 JSON 전송
    bool send_to(int client_fd, const std::string& json_message);

    // 현재 연결된 세션 수
    std::size_t session_count() const;

private:
    SessionManager() = default;

    bool send_json(int fd, const std::string& json_body);

    mutable std::mutex                       mutex_;
    std::unordered_map<int, GuiSession>      sessions_;   // key: client_fd
};

} // namespace factory
