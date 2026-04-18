// ============================================================================
// station_handler.cpp — 검사 이벤트 → Service 호출 → ACK/GUI 발행
// ============================================================================
#include "handler/station_handler.h"
#include "core/logger.h"
#include "Protocol.h"

namespace factory {

// ===== Station1Handler (입고검사) =============================================

Station1Handler::Station1Handler(EventBus& bus, InspectionService& service)
    : event_bus_(bus), service_(service) {
}

void Station1Handler::register_handlers() {
    event_bus_.subscribe(EventType::INSPECTION_INBOUND,
                         [this](const std::any& p) { this->on_inspection(p); });
}

void Station1Handler::on_inspection(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    log_ai("입고검사 NG 수신 | 점수=%.2f 결함=%s", ev.score, ev.defect_type.c_str());

    auto result = service_.process(ev);

    if (result.success) {
        // DB+이미지 성공 → ACK + GUI 푸시
        event_bus_.publish(EventType::DB_WRITE_COMPLETED, ev);
        event_bus_.publish(EventType::GUI_PUSH_REQUESTED, ev);
    } else {
        // 실패 → NACK
        AckSendEvent nack{};
        nack.protocol_no   = static_cast<int>(
            ack_no_for(static_cast<ProtocolNo>(ev.protocol_no)));
        nack.inspection_id = ev.inspection_id;
        nack.sender_addr   = ev.sender_addr;
        nack.ack_ok        = false;
        nack.error_message = result.error_message;
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, nack);
    }
}

// ===== Station2Handler (조립검사) =============================================

Station2Handler::Station2Handler(EventBus& bus, InspectionService& service)
    : event_bus_(bus), service_(service) {
}

void Station2Handler::register_handlers() {
    event_bus_.subscribe(EventType::INSPECTION_ASSEMBLY,
                         [this](const std::any& p) { this->on_inspection(p); });
}

void Station2Handler::on_inspection(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    log_ai("조립검사 NG 수신 | 점수=%.2f 결함=%s", ev.score, ev.defect_type.c_str());

    auto result = service_.process(ev);

    if (result.success) {
        event_bus_.publish(EventType::DB_WRITE_COMPLETED, ev);
        event_bus_.publish(EventType::GUI_PUSH_REQUESTED, ev);
    } else {
        AckSendEvent nack{};
        nack.protocol_no   = static_cast<int>(
            ack_no_for(static_cast<ProtocolNo>(ev.protocol_no)));
        nack.inspection_id = ev.inspection_id;
        nack.sender_addr   = ev.sender_addr;
        nack.ack_ok        = false;
        nack.error_message = result.error_message;
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, nack);
    }
}

} // namespace factory
