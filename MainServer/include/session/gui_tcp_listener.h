// ============================================================================
// gui_tcp_listener.h — MFC GUI 클라이언트 전용 TCP 리스너
// ============================================================================
// 책임: TCP 접속 수락 + 패킷 수신만 담당.
// 수신된 JSON은 GuiRouter에 전달하여 프로토콜별 처리를 위임한다.
// ============================================================================
#pragma once

#include "core/event_bus.h"
#include "session/gui_router.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace factory {

class GuiTcpListener {
public:
    GuiTcpListener(EventBus& bus, uint16_t port, GuiRouter& router);
    ~GuiTcpListener();

    void start();
    void stop();

private:
    void run_accept_loop();
    void handle_client(int client_fd, const std::string& remote_addr);
    bool recv_one_request(int client_fd, std::string& out_json);

    EventBus&         event_bus_;
    uint16_t          listen_port_;
    int               server_fd_;
    std::thread       accept_thread_;
    std::atomic<bool> is_running_;
    GuiRouter&        router_;
};

} // namespace factory
