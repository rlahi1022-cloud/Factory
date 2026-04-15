// GuiNotifier.cpp
#include "GuiNotifier.h"

#include <iostream>

namespace factory {

GuiNotifier::GuiNotifier(EventBus& bus)
    : event_bus_(bus) {
}

void GuiNotifier::register_handlers() {
    event_bus_.subscribe(EventType::GUI_PUSH_REQUESTED,
                         [this](const std::any& p) { this->on_gui_push(p); });
    event_bus_.subscribe(EventType::SERVER_DOWN,
                         [this](const std::any& p) { this->on_gui_push(p); });
    event_bus_.subscribe(EventType::SERVER_RECOVERED,
                         [this](const std::any& p) { this->on_gui_push(p); });
}

void GuiNotifier::on_gui_push(const std::any& /*payload*/) {
    // TODO: MFC 클라이언트로 메시지 푸시
    // 옵션 1: 별도 TCP 채널로 클라이언트에 broadcast
    // 옵션 2: 동일 프로세스라면 ::PostMessage(hwnd, WM_USER+1, ...)
    std::cout << "[GuiNotifier] push to GUI client" << std::endl;
}

} // namespace factory
