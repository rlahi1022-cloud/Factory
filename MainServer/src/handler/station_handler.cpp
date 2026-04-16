// StationHandler.cpp
#include "handler/station_handler.h"

#include <iostream>

namespace factory {

// ===== Station1Handler (입고 검사) =====

Station1Handler::Station1Handler(EventBus& bus)
    : event_bus_(bus) {
}

void Station1Handler::register_handlers() {
    event_bus_.subscribe(EventType::INSPECTION_INBOUND,
                         [this](const std::any& p) { this->on_inspection(p); });
}

void Station1Handler::on_inspection(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    std::cout << "[Station1Handler] NG inbound — score=" << ev.score
              << " defect=" << ev.defect_type << std::endl;

    // 검증 통과 → 후속 이벤트 발행
    event_bus_.publish(EventType::INSPECTION_VALIDATED, ev);

    if (!ev.image_bytes.empty()) {
        event_bus_.publish(EventType::IMAGE_SAVE_REQUESTED, ev);
    }
    event_bus_.publish(EventType::DB_WRITE_REQUESTED, ev);
    event_bus_.publish(EventType::GUI_PUSH_REQUESTED, ev);
}

// ===== Station2Handler (조립 검사) =====

Station2Handler::Station2Handler(EventBus& bus)
    : event_bus_(bus) {
}

void Station2Handler::register_handlers() {
    event_bus_.subscribe(EventType::INSPECTION_ASSEMBLY,
                         [this](const std::any& p) { this->on_inspection(p); });
}

void Station2Handler::on_inspection(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    std::cout << "[Station2Handler] NG assembly — score=" << ev.score
              << " defect=" << ev.defect_type << std::endl;

    // assemblies 테이블용 부가 정보(cap_ok/label_ok/fill_ok/yolo_detections)는
    // ev.raw_json에서 추출하여 DbManager에 위임 — 여기서는 골격만 둠

    event_bus_.publish(EventType::INSPECTION_VALIDATED, ev);

    if (!ev.image_bytes.empty()) {
        event_bus_.publish(EventType::IMAGE_SAVE_REQUESTED, ev);
    }
    event_bus_.publish(EventType::DB_WRITE_REQUESTED, ev);
    event_bus_.publish(EventType::GUI_PUSH_REQUESTED, ev);
}

} // namespace factory
