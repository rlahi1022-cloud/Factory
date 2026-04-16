#pragma once
// Router.h
// PACKET_RECEIVED 이벤트를 구독해 JSON을 파싱하고
// station 필드에 따라 INSPECTION_INBOUND / INSPECTION_ASSEMBLY 이벤트로 재발행.

#include "core/event_bus.h"

namespace factory {

class Router {
public:
    explicit Router(EventBus& bus);

    // EventBus에 핸들러 등록
    void register_handlers();

private:
    void on_packet_received(const std::any& payload);

    // JSON에서 단순 string/int/double 필드 추출 (가벼운 파서)
    static std::string extract_str(const std::string& json, const std::string& key);
    static int         extract_int(const std::string& json, const std::string& key);
    static double      extract_double(const std::string& json, const std::string& key);

    EventBus& event_bus_;
};

} // namespace factory
