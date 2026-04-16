#pragma once
// gui_tcp_listener.h
// MFC GUI 클라이언트 전용 TCP 리스너
// - 추론서버용 TcpListener와 별도 포트로 동작
// - 접속 시 SessionManager에 세션 등록
// - 클라이언트로부터 요청 수신 → EventBus에 발행

#include "core/event_bus.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace factory {

class GuiTcpListener {
public:
    GuiTcpListener(EventBus& bus, uint16_t port);
    ~GuiTcpListener();

    void start();
    void stop();

private:
    void run_accept_loop();
    void handle_client(int client_fd, const std::string& remote_addr);

    // 클라이언트 요청 패킷 1건 수신
    bool recv_one_request(int client_fd, std::string& out_json);

    EventBus&         event_bus_;
    uint16_t          listen_port_;
    int               server_fd_;
    std::thread       accept_thread_;
    std::atomic<bool> is_running_;
};

} // namespace factory
