#pragma once
// GuiNotifier.h
// GUI_PUSH_REQUESTED 이벤트 구독 → MFC 클라이언트에 결과 푸시.
// 실제 환경: MFC 측 윈도우 핸들에 ::PostMessage 또는 별도 TCP 채널.

#include "event_bus.h"

namespace factory {

class GuiNotifier {
public:
    explicit GuiNotifier(EventBus& bus);
    void register_handlers();

private:
    void on_gui_push(const std::any& payload);
    EventBus& event_bus_;
};

} // namespace factory
