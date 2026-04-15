// Router.cpp
#include "Router.h"
#include "Protocol.h"

#include <cstdlib>
#include <iostream>

namespace factory {

Router::Router(EventBus& bus)
    : event_bus_(bus) {
}

void Router::register_handlers() {
    event_bus_.subscribe(EventType::PACKET_RECEIVED,
                         [this](const std::any& p) { this->on_packet_received(p); });
}

void Router::on_packet_received(const std::any& payload) {
    const auto& packet = std::any_cast<const PacketReceivedEvent&>(payload);

    // type 필드 확인
    std::string msg_type = extract_str(packet.json_payload, "type");
    if (msg_type != MsgType::INSPECT) {
        // heartbeat 등은 다른 핸들러(HealthChecker)에서 처리하도록 분기 가능
        return;
    }

    int station_id = extract_int(packet.json_payload, "station");

    InspectionEvent ev{};
    ev.station_id  = station_id;
    ev.result      = extract_str(packet.json_payload, "result");
    ev.defect_type = extract_str(packet.json_payload, "defect");
    ev.score       = extract_double(packet.json_payload, "score");
    ev.latency_ms  = extract_int(packet.json_payload, "latency_ms");
    ev.timestamp   = extract_str(packet.json_payload, "timestamp");
    ev.image_bytes = packet.image_bytes;
    ev.raw_json    = packet.json_payload;

    if (station_id == static_cast<int>(StationId::INBOUND)) {
        event_bus_.publish(EventType::INSPECTION_INBOUND, ev);
    } else if (station_id == static_cast<int>(StationId::ASSEMBLY)) {
        event_bus_.publish(EventType::INSPECTION_ASSEMBLY, ev);
    } else {
        std::cerr << "[Router] unknown station id: " << station_id << std::endl;
    }
}

// --- 가벼운 JSON 추출 (production에서는 nlohmann/json 사용 권장) ---

std::string Router::extract_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto first_quote = json.find('"', colon);
    if (first_quote == std::string::npos) return "";
    auto last_quote = json.find('"', first_quote + 1);
    if (last_quote == std::string::npos) return "";
    return json.substr(first_quote + 1, last_quote - first_quote - 1);
}

int Router::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

double Router::extract_double(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0.0;
    return std::strtod(json.c_str() + colon + 1, nullptr);
}

} // namespace factory
