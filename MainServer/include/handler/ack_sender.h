#pragma once
// AckSender.h
// DB_WRITE_COMPLETED 또는 ACK_SEND_REQUESTED 이벤트를 구독해
// 추론서버로 ACK(STATION1_NG_ACK=1001 / STATION2_NG_ACK=1003)를 회신.
// ConnectionRegistry로 sender_addr → client_fd 조회 후 같은 connection에 송신.

#include "core/event_bus.h"

namespace factory {

class AckSender {
public:
    explicit AckSender(EventBus& bus);
    void register_handlers();

private:
    void on_db_write_completed(const std::any& payload);
    void on_ack_send_requested(const std::any& payload);

    bool send_ack(const std::string& sender_addr,
                  int protocol_no,
                  const std::string& inspection_id,
                  bool ack_ok,
                  const std::string& error_message);

    EventBus& event_bus_;
};

} // namespace factory
