// ============================================================================
// gui_notifier.cpp — EventBus 이벤트 → MFC 클라이언트 JSON 푸시 구현
// ============================================================================
// 각 핸들러는 이벤트 페이로드를 JSON으로 직렬화한 뒤,
// SessionManager::broadcast()를 통해 연결된 GUI 클라이언트에 전송한다.
// ============================================================================
#include "session/gui_notifier.h"
#include "session/session_manager.h"

#include "core/logger.h"

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
    event_bus_.subscribe(EventType::OK_COUNT_RECEIVED,
                         [this](const std::any& p) { this->on_ok_count(p); });
    event_bus_.subscribe(EventType::TRAIN_PROGRESS_RECEIVED,
                         [this](const std::any& p) { this->on_train_progress(p); });
    event_bus_.subscribe(EventType::TRAIN_COMPLETE_RECEIVED,
                         [this](const std::any& p) { this->on_train_complete(p); });
    event_bus_.subscribe(EventType::TRAIN_FAIL_RECEIVED,
                         [this](const std::any& p) { this->on_train_fail(p); });
}

// NG 검출 시 해당 station 구독자에게만 푸시 (protocol 110)
void GuiNotifier::on_gui_push(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":110"
       << ",\"inspection_id\":\"" << ev.inspection_id << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"result\":\"" << ev.result << "\""
       << ",\"defect_type\":\"" << ev.defect_type << "\""
       << ",\"score\":" << ev.score
       << ",\"latency_ms\":" << ev.latency_ms
       << ",\"timestamp\":\"" << ev.timestamp << "\""
       << "}";

    SessionManager::instance().broadcast(os.str(), ev.station_id);
    log_push("NG 푸시 | 스테이션=%d 접속자=%zu명", ev.station_id,
             SessionManager::instance().session_count());
}

// 서버 장애/복구 알림 — 전체 클라이언트에 브로드캐스트 (protocol 170)
void GuiNotifier::on_server_status(const std::any& payload, bool is_down) {
    const auto& ev = std::any_cast<const ServerStatusEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":170"
       << ",\"server_name\":\"" << ev.server_name << "\""
       << ",\"ip\":\"" << ev.ip << "\""
       << ",\"port\":" << ev.port
       << ",\"status\":\"" << (is_down ? "down" : "recovered") << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    if (is_down)
        log_push("서버 장애 감지 | %s", ev.server_name.c_str());
    else
        log_push("서버 복구 감지 | %s", ev.server_name.c_str());
}

// 양품/불량 집계 카운트 갱신 푸시 (protocol 112)
void GuiNotifier::on_ok_count(const std::any& payload) {
    const auto& ev = std::any_cast<const OkCountEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":112"
       << ",\"station_id\":" << ev.station_id
       << ",\"ok_count\":" << ev.ok_count
       << ",\"ng_count\":" << ev.ng_count
       << ",\"latency_avg\":" << ev.latency_avg
       << ",\"period\":\"" << ev.period << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
}

// 재학습 진행률 푸시 (protocol 154, status="진행중")
void GuiNotifier::on_train_progress(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainProgressEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":154"
       << ",\"request_id\":\"" << ev.request_id << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"model_type\":\"" << ev.model_type << "\""
       << ",\"progress\":" << ev.progress
       << ",\"epoch\":" << ev.epoch
       << ",\"loss\":" << ev.loss
       << ",\"status\":\"" << ev.status << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    log_push("학습 진행률 푸시 | 스테이션=%d 진행=%d%%", ev.station_id, ev.progress);
}

// 재학습 완료 푸시 (protocol 154, progress=100, status="완료")
void GuiNotifier::on_train_complete(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainCompleteEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":154"
       << ",\"request_id\":\"" << ev.request_id << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"model_type\":\"" << ev.model_type << "\""
       << ",\"progress\":100"
       << ",\"status\":\"완료\""
       << ",\"version\":\"" << ev.version << "\""
       << ",\"accuracy\":" << ev.accuracy
       << ",\"message\":\"" << ev.message << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    log_push("학습 완료 푸시 | 스테이션=%d 버전=%s", ev.station_id, ev.version.c_str());
}

// 재학습 실패 푸시 (protocol 154, progress=-1, status="실패")
void GuiNotifier::on_train_fail(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainFailEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":154"
       << ",\"request_id\":\"" << ev.request_id << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"model_type\":\"" << ev.model_type << "\""
       << ",\"progress\":-1"
       << ",\"status\":\"실패\""
       << ",\"message\":\"" << ev.message << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    log_push("학습 실패 푸시 | 스테이션=%d 사유=%s", ev.station_id, ev.message.c_str());
}

} // namespace factory
