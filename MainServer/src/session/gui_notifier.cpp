// gui_notifier.cpp
#include "session/gui_notifier.h"
#include "session/session_manager.h"

#include <iostream>
#include <sstream>

namespace factory {

GuiNotifier::GuiNotifier(EventBus& bus)
    : event_bus_(bus) {
}

void GuiNotifier::register_handlers() {
    event_bus_.subscribe(EventType::GUI_PUSH_REQUESTED,
                         [this](const std::any& p) { this->on_gui_push(p); });
    event_bus_.subscribe(EventType::SERVER_DOWN,
                         [this](const std::any& p) { this->on_server_status(p, true); });
    event_bus_.subscribe(EventType::SERVER_RECOVERED,
                         [this](const std::any& p) { this->on_server_status(p, false); });
}

void GuiNotifier::on_gui_push(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    // NG 결과를 GUI 클라이언트에 push (Protocol 110: INSPECT_NG_PUSH)
    std::ostringstream os;
    os << "{"
       << "\"protocol_no\":110,"
       << "\"inspection_id\":\"" << ev.inspection_id << "\","
       << "\"station_id\":" << ev.station_id << ","
       << "\"result\":\"" << ev.result << "\","
       << "\"defect_type\":\"" << ev.defect_type << "\","
       << "\"score\":" << ev.score << ","
       << "\"latency_ms\":" << ev.latency_ms << ","
       << "\"timestamp\":\"" << ev.timestamp << "\""
       << "}";

    SessionManager::instance().broadcast(os.str(), ev.station_id);
    std::cout << "[GuiNotifier] push NG result station=" << ev.station_id
              << " to " << SessionManager::instance().session_count()
              << " client(s)" << std::endl;
}

void GuiNotifier::on_server_status(const std::any& payload, bool is_down) {
    const auto& ev = std::any_cast<const ServerStatusEvent&>(payload);

    std::ostringstream os;
    os << "{"
       << "\"protocol_no\":170,"
       << "\"server_name\":\"" << ev.server_name << "\","
       << "\"ip\":\"" << ev.ip << "\","
       << "\"port\":" << ev.port << ","
       << "\"status\":\"" << (is_down ? "down" : "recovered") << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    std::cout << "[GuiNotifier] server " << ev.server_name
              << (is_down ? " DOWN" : " RECOVERED") << std::endl;
}

} // namespace factory
