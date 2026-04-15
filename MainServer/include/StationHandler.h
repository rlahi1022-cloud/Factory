#pragma once
// StationHandler.h
// INSPECTION_INBOUND / INSPECTION_ASSEMBLY 이벤트를 받아 도메인 검증 후
// INSPECTION_VALIDATED, IMAGE_SAVE_REQUESTED, DB_WRITE_REQUESTED, GUI_PUSH_REQUESTED를 발행.

#include "EventBus.h"

namespace factory {

class Station1Handler {
public:
    explicit Station1Handler(EventBus& bus);
    void register_handlers();

private:
    void on_inspection(const std::any& payload);
    EventBus& event_bus_;
};

class Station2Handler {
public:
    explicit Station2Handler(EventBus& bus);
    void register_handlers();

private:
    void on_inspection(const std::any& payload);
    EventBus& event_bus_;
};

} // namespace factory
