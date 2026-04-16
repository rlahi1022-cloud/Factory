// Router.cpp
#include "router.h"
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

    int protocol_no = extract_int(packet.json_payload, "protocol_no");
    auto no = static_cast<ProtocolNo>(protocol_no);

    switch (no) {
        case ProtocolNo::STATION1_NG:
        case ProtocolNo::STATION2_NG: {
            InspectionEvent ev{};
            ev.protocol_no    = protocol_no;
            ev.inspection_id  = extract_str(packet.json_payload, "inspection_id");
            ev.station_id     = extract_int(packet.json_payload, "station_id");
            ev.result         = extract_str(packet.json_payload, "result");
            ev.defect_type    = extract_str(packet.json_payload, "defect");
            ev.score          = extract_double(packet.json_payload, "score");
            ev.latency_ms     = extract_int(packet.json_payload, "latency_ms");
            ev.timestamp      = extract_str(packet.json_payload, "timestamp");
            ev.image_bytes    = packet.image_bytes;
            ev.raw_json       = packet.json_payload;
            ev.sender_addr    = packet.remote_addr;

            if (no == ProtocolNo::STATION1_NG) {
                event_bus_.publish(EventType::INSPECTION_INBOUND, ev);
            } else {
                event_bus_.publish(EventType::INSPECTION_ASSEMBLY, ev);
            }
            break;
        }

        case ProtocolNo::STATION_OK_COUNT: {
            OkCountEvent ev{};
            ev.station_id  = extract_int(packet.json_payload, "station_id");
            ev.ok_count    = extract_int(packet.json_payload, "ok_count");
            ev.ng_count    = extract_int(packet.json_payload, "ng_count");
            ev.latency_avg = extract_double(packet.json_payload, "latency_avg");
            ev.period      = extract_str(packet.json_payload, "period");
            event_bus_.publish(EventType::OK_COUNT_RECEIVED, ev);
            break;
        }

        case ProtocolNo::INSPECT_META: {
            InspectMetaEvent ev{};
            ev.inspection_id = extract_str(packet.json_payload, "inspection_id");
            ev.station_id    = extract_int(packet.json_payload, "station_id");
            ev.timestamp     = extract_str(packet.json_payload, "timestamp");
            ev.latency_ms    = extract_int(packet.json_payload, "latency_ms");
            ev.model_id      = extract_int(packet.json_payload, "model_id");
            ev.result        = extract_str(packet.json_payload, "result");
            event_bus_.publish(EventType::INSPECT_META_RECEIVED, ev);
            break;
        }

        case ProtocolNo::HEALTH_PONG:
            // HealthChecker가 별도 채널을 쓴다면 여기서 흡수
            break;

        case ProtocolNo::MODEL_RELOAD_RES:
            std::cout << "[Router] MODEL_RELOAD_RES received" << std::endl;
            break;

        default:
            std::cerr << "[Router] unhandled protocol_no=" << protocol_no << std::endl;
            break;
    }
}

// --- 가벼운 JSON 추출 ---

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
