#pragma once
// gui_notifier.h
// GUI_PUSH_REQUESTED / SERVER_DOWN / SERVER_RECOVERED / OK_COUNT_RECEIVED 구독
// → SessionManager를 통해 연결된 MFC 클라이언트에 JSON broadcast

#include "core/event_bus.h"

namespace factory {

class GuiNotifier {
public:
    explicit GuiNotifier(EventBus& bus);
    void register_handlers();

private:
    void on_gui_push(const std::any& payload);
    void on_server_status(const std::any& payload, bool is_down);
    void on_ok_count(const std::any& payload);

    EventBus& event_bus_;
};

} // namespace factory
