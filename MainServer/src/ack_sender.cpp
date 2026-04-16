// AckSender.cpp
#include "ack_sender.h"
#include "connection_registry.h"
#include "Protocol.h"

#include <cstdint>
#include <iostream>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <sys/socket.h>
#endif

namespace factory {

AckSender::AckSender(EventBus& bus)
    : event_bus_(bus) {
}

void AckSender::register_handlers() {
    event_bus_.subscribe(EventType::DB_WRITE_COMPLETED,
                         [this](const std::any& p) { this->on_db_write_completed(p); });
    event_bus_.subscribe(EventType::ACK_SEND_REQUESTED,
                         [this](const std::any& p) { this->on_ack_send_requested(p); });
}

void AckSender::on_db_write_completed(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    int ack_no = static_cast<int>(
        ack_no_for(static_cast<ProtocolNo>(ev.protocol_no)));
    send_ack(ev.sender_addr, ack_no, ev.inspection_id, true, "");
}

void AckSender::on_ack_send_requested(const std::any& payload) {
    const auto& ev = std::any_cast<const AckSendEvent&>(payload);
    send_ack(ev.sender_addr, ev.protocol_no, ev.inspection_id,
             ev.ack_ok, ev.error_message);
}

bool AckSender::send_ack(const std::string& sender_addr,
                         int protocol_no,
                         const std::string& inspection_id,
                         bool ack_ok,
                         const std::string& error_message) {
    int fd = ConnectionRegistry::instance().find_fd(sender_addr);
    if (fd < 0) {
        std::cerr << "[AckSender] no connection for " << sender_addr << std::endl;
        return false;
    }

    // ACK JSON 빌드 (가벼운 수동 구성, 외부 의존성 없음)
    std::ostringstream os;
    os << "{"
       << "\"protocol_no\":" << protocol_no << ","
       << "\"protocol_version\":\"" << PROTOCOL_VERSION << "\","
       << "\"inspection_id\":\"" << inspection_id << "\","
       << "\"ack\":" << (ack_ok ? "true" : "false");
    if (!ack_ok) {
        os << ",\"error_message\":\"" << error_message << "\"";
    }
    os << ",\"image_size\":0"
       << "}";
    std::string json_body = os.str();

    // [4byte length BE] + [JSON]
    uint32_t json_size = static_cast<uint32_t>(json_body.size());
    uint8_t  header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),
        static_cast<uint8_t>((json_size >> 16) & 0xFF),
        static_cast<uint8_t>((json_size >>  8) & 0xFF),
        static_cast<uint8_t>( json_size        & 0xFF),
    };

    int sent_h = static_cast<int>(::send(fd, reinterpret_cast<const char*>(header), 4, 0));
    int sent_b = static_cast<int>(::send(fd, json_body.c_str(),
                                         static_cast<int>(json_body.size()), 0));
    if (sent_h != 4 || sent_b != static_cast<int>(json_body.size())) {
        std::cerr << "[AckSender] send failed (fd=" << fd
                  << " addr=" << sender_addr << ")" << std::endl;
        return false;
    }
    std::cout << "[AckSender] sent ACK no=" << protocol_no
              << " inspection_id=" << inspection_id
              << " → " << sender_addr << std::endl;
    return true;
}

} // namespace factory
