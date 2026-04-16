#pragma once
// TcpListener.h
// TCP 서버 — AI 추론 서버들로부터 검사 결과 패킷 수신
// 수신 패킷은 PACKET_RECEIVED 이벤트로 EventBus에 발행

#include "core/event_bus.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace factory {

class TcpListener {
public:
    TcpListener(EventBus& bus, uint16_t port);
    ~TcpListener();

    void start();
    void stop();

private:
    void run_accept_loop();
    void handle_client(int client_fd, const std::string& remote_addr);

    // 패킷 한 건 수신 (length-prefixed JSON + optional image)
    bool recv_one_packet(int client_fd,
                         std::string& out_json,
                         std::vector<uint8_t>& out_image);

    EventBus&         event_bus_;
    uint16_t          listen_port_;
    int               server_fd_;
    std::thread       accept_thread_;
    std::atomic<bool> is_running_;
};

} // namespace factory
