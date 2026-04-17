// ============================================================================
// gui_tcp_listener.h — MFC GUI 클라이언트 전용 TCP 리스너 + 요청 핸들러
// ============================================================================
// 목적:
//   MFC 클라이언트 전용 TCP 포트에서 접속을 수락하고, 수신된 JSON 요청을
//   프로토콜 번호(100~199)에 따라 라우팅하여 DAO를 통해 DB 조회 후 응답 전송.
//
// DB 접근:
//   ConnectionPool에서 커넥션을 빌려 DAO를 통해 사용한다.
//   DbManager와 같은 풀을 공유하므로 별도 DB 연결이 불필요하다.
// ============================================================================
#pragma once

#include "core/event_bus.h"
#include "storage/connection_pool.h"
#include "storage/dao.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace factory {

class GuiTcpListener {
public:
    /// ConnectionPool을 외부에서 주입받는다
    GuiTcpListener(EventBus& bus, uint16_t port, ConnectionPool& pool);
    ~GuiTcpListener();

    void start();
    void stop();

private:
    void run_accept_loop();
    void handle_client(int client_fd, const std::string& remote_addr);
    bool recv_one_request(int client_fd, std::string& out_json);

    void route_request(int client_fd, const std::string& remote_addr,
                       const std::string& json_request);

    // 개별 프로토콜 핸들러
    void handle_login_req(int client_fd, const std::string& json);
    void handle_logout_req(int client_fd, const std::string& json);
    void handle_inspect_history_req(int client_fd, const std::string& json);
    void handle_stats_req(int client_fd, const std::string& json);
    void handle_model_list_req(int client_fd, const std::string& json);
    void handle_retrain_req(int client_fd, const std::string& json);
    void handle_register_req(int client_fd, const std::string& json);

    // 유틸리티
    static std::string extract_str(const std::string& json, const std::string& key);
    static int extract_int(const std::string& json, const std::string& key);
    static bool send_json(int fd, const std::string& json_body);
    static std::string get_timestamp();
    static std::string escape_json(const std::string& s);

    EventBus&         event_bus_;
    uint16_t          listen_port_;
    int               server_fd_;
    std::thread       accept_thread_;
    std::atomic<bool> is_running_;

    // DAO — ConnectionPool 기반
    ConnectionPool& pool_;
    UserDao         user_dao_;
    ModelDao        model_dao_;
    StatsDao        stats_dao_;
};

} // namespace factory
