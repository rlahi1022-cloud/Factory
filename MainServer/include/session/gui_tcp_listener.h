#pragma once
// gui_tcp_listener.h
// MFC GUI 클라이언트 전용 TCP 리스너 + 요청 핸들러
// protocol 100~199 요청을 파싱하여 DB 조회 후 응답 전송

#include "core/event_bus.h"

#include <mariadb/mysql.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace factory {

class GuiTcpListener {
public:
    GuiTcpListener(EventBus& bus, uint16_t port,
                   const std::string& db_host,
                   const std::string& db_user,
                   const std::string& db_password,
                   const std::string& db_schema,
                   unsigned int db_port = 3306);
    ~GuiTcpListener();

    void start();
    void stop();

private:
    void run_accept_loop();
    void handle_client(int client_fd, const std::string& remote_addr);
    bool recv_one_request(int client_fd, std::string& out_json);

    // 요청 라우팅
    void route_request(int client_fd, const std::string& remote_addr,
                       const std::string& json_request);

    // protocol 핸들러
    void handle_login_req(int client_fd, const std::string& json);
    void handle_logout_req(int client_fd, const std::string& json);
    void handle_inspect_history_req(int client_fd, const std::string& json);
    void handle_stats_req(int client_fd, const std::string& json);
    void handle_model_list_req(int client_fd, const std::string& json);
    void handle_retrain_req(int client_fd, const std::string& json);
    void handle_register_req(int client_fd, const std::string& json);

    // DB 연결
    bool db_connect();
    void db_disconnect();

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

    // GUI 요청용 DB 연결 (DbManager와 별도)
    MYSQL*            db_conn_;
    std::mutex        db_mutex_;
    std::string       db_host_;
    std::string       db_user_;
    std::string       db_password_;
    std::string       db_schema_;
    unsigned int      db_port_;
};

} // namespace factory
