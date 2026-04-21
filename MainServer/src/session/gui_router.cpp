// ============================================================================
// gui_router.cpp — GUI 클라이언트 요청 라우터 구현
// ============================================================================
// protocol_no별로 GuiService를 호출하고 JSON 응답을 생성하여 전송한다.
// ============================================================================
#include "session/gui_router.h"
#include "session/session_manager.h"
#include "monitor/connection_registry.h"
#include "core/config.h"
#include "core/logger.h"
#include "core/tcp_utils.h"
#include "security/json_safety.h"
#include "Protocol.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace factory {

GuiRouter::GuiRouter(GuiService& service)
    : service_(service) {
}

void GuiRouter::route(int client_fd, const std::string& remote_addr,
                      const std::string& json_request) {
    int protocol_no = extract_int(json_request, "protocol_no");

    switch (protocol_no) {
        case static_cast<int>(ProtocolNo::LOGIN_REQ):
            handle_login(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::LOGOUT_REQ):
            handle_logout(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::REGISTER_REQ):
            handle_register(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::INSPECT_HISTORY_REQ):
            handle_inspect_history(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::STATS_REQ):
            handle_stats(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::MODEL_LIST_REQ):
            handle_model_list(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::RETRAIN_REQ):
            handle_retrain(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::EXT_ACK):
            break;
        default:
            log_clt("미처리 프로토콜 | no=%d ip=%s", protocol_no, remote_addr.c_str());
            break;
    }
}

// ── LOGIN ────────────────────────────────────────────────────────────

void GuiRouter::handle_login(int fd, const std::string& json) {
    std::string username   = extract_str(json, "username");
    std::string password   = extract_str(json, "password");
    std::string request_id = extract_str(json, "request_id");

    log_clt("로그인 요청 | 사용자=%s", username.c_str());

    auto result = service_.login(username, password);

    if (result.success) {
        // 중복 로그인 정책:
        //   - 같은 IP에서 온 중복 (LoginDlg→MainTabDlg 전환) → 조용히 교체
        //   - 다른 IP에서 온 중복 (실제 다중접속 시도) → 새 요청 거부, 기존 세션 유지
        int existing_fd = SessionManager::instance().find_fd_by_username(username);
        if (existing_fd >= 0 && existing_fd != fd) {
            auto existing_addr = SessionManager::instance().get_remote_addr(existing_fd);
            auto new_addr      = SessionManager::instance().get_remote_addr(fd);

            // "IP:PORT" → "IP"만 추출 (마지막 ':' 앞까지)
            auto ip_of = [](const std::string& addr) {
                auto pos = addr.rfind(':');
                return (pos != std::string::npos) ? addr.substr(0, pos) : addr;
            };
            std::string existing_ip = ip_of(existing_addr);
            std::string new_ip      = ip_of(new_addr);

            if (existing_ip == new_ip && !existing_ip.empty()) {
                // 같은 PC에서 재연결 — 브로드캐스트 타겟에서 기존 세션 제거만
                SessionManager::instance().unregister_session(existing_fd);
            } else {
                // 다른 PC에서 동시 로그인 시도 — 거부
                log_clt("동시접속 거부 | 사용자=%s 기존ip=%s 신규ip=%s",
                        username.c_str(), existing_ip.c_str(), new_ip.c_str());

                std::ostringstream rej;
                rej << "{\"protocol_no\":101"
                    << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
                    << ",\"request_id\":\"" << escape_json(request_id) << "\""
                    << ",\"success\":false"
                    << ",\"username\":\"" << escape_json(username) << "\""
                    << ",\"role\":\"\""
                    << ",\"employee_id\":\"\""
                    << ",\"message\":\"다른 위치에서 이미 로그인된 계정입니다.\""
                    << ",\"timestamp\":\"" << get_timestamp() << "\"}";
                send_json(fd, rej.str());
                return;
            }
        }
        SessionManager::instance().set_client_info(fd, username, 0);
    }

    std::ostringstream os;
    os << "{\"protocol_no\":101"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"username\":\"" << escape_json(username) << "\""
       << ",\"role\":\"" << escape_json(result.role) << "\""
       << ",\"employee_id\":\"" << escape_json(result.employee_id) << "\""
       << ",\"message\":\"" << (result.success ? "로그인 성공" : "인증 실패") << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());

    // ── 로그인 성공 시 현재 서버 상태(LED) 초기 동기화 ──
    // 목적: 클라이언트가 접속한 시점에는 HealthChecker의 "상태 전환" 이벤트를
    //       놓친 상태이므로 초기 LED를 알 수 없다.
    //       config의 health_check 타겟별로 현재 ConnectionRegistry 조회하여
    //       현재 상태(alive/down)를 HEALTH_PUSH(170)로 즉시 전송한다.
    if (result.success) {
        auto& cfg = Config::instance();
        auto connections = ConnectionRegistry::instance().get_all_connections();

        for (const auto& target : cfg.get_health_targets()) {
            // target.ip로 시작하는 연결이 있는지 확인 (HealthChecker와 동일 규칙)
            std::string ip_prefix = target.ip + ":";
            bool alive = false;
            for (const auto& [addr, conn_fd] : connections) {
                if (addr.rfind(ip_prefix, 0) == 0) { alive = true; break; }
            }

            // HEALTH_PUSH(170) 전송 — 방금 로그인한 이 클라이언트에게만
            std::ostringstream hp;
            hp << "{\"protocol_no\":170"
               << ",\"server_name\":\"" << escape_json(target.name) << "\""
               << ",\"ip\":\""          << escape_json(target.ip)   << "\""
               << ",\"port\":"          << target.port
               << ",\"status\":\""      << (alive ? "recovered" : "down") << "\""
               << "}";
            send_json(fd, hp.str());
        }
        log_clt("초기 서버 상태 동기화 완료 | fd=%d", fd);
    }
}

// ── REGISTER ─────────────────────────────────────────────────────────

void GuiRouter::handle_register(int fd, const std::string& json) {
    std::string username    = extract_str(json, "username");
    std::string password    = extract_str(json, "password");
    std::string employee_id = extract_str(json, "employee_id");
    std::string role        = extract_str(json, "role");
    std::string request_id  = extract_str(json, "request_id");

    log_clt("회원가입 요청 | 사용자=%s 사원ID=%s", username.c_str(), employee_id.c_str());

    auto result = service_.register_user(employee_id, username, password, role);

    std::ostringstream os;
    os << "{\"protocol_no\":105"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"message\":\"" << escape_json(result.message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── LOGOUT ───────────────────────────────────────────────────────────

void GuiRouter::handle_logout(int fd, const std::string& json) {
    std::string username = extract_str(json, "username");
    log_clt("로그아웃 | 사용자=%s", username.c_str());

    std::ostringstream os;
    os << "{\"protocol_no\":103"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"success\":true"
       << ",\"message\":\"로그아웃 완료\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── INSPECT_HISTORY ──────────────────────────────────────────────────

void GuiRouter::handle_inspect_history(int fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");
    int limit              = extract_int(json, "limit");

    auto records = service_.get_history(station_filter, date_from, date_to, limit);

    std::ostringstream items;
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        if (i > 0) items << ",";
        items << "{\"id\":" << r.id
              << ",\"station_id\":" << r.station_id
              << ",\"timestamp\":\"" << escape_json(r.timestamp) << "\""
              << ",\"result\":\"" << escape_json(r.result) << "\""
              << ",\"confidence\":" << r.confidence
              << ",\"defect_type\":\"" << escape_json(r.defect_type) << "\""
              << ",\"image_path\":\"" << escape_json(r.image_path) << "\""
              << ",\"latency_ms\":" << r.latency_ms
              << "}";
    }

    std::ostringstream os;
    os << "{\"protocol_no\":115"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"count\":" << records.size()
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
    log_clt("검사이력 응답 | %zu건", records.size());
}

// ── STATS ────────────────────────────────────────────────────────────

void GuiRouter::handle_stats(int fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");

    auto s = service_.get_stats(station_filter, date_from, date_to);

    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{\"protocol_no\":131"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"total\":" << s.total
       << ",\"ok_count\":" << s.ok_count
       << ",\"ng_count\":" << s.ng_count
       << ",\"ng_rate\":" << s.ng_rate
       << ",\"s1_ok\":" << s.s1_ok << ",\"s1_ng\":" << s.s1_ng
       << ",\"s2_ok\":" << s.s2_ok << ",\"s2_ng\":" << s.s2_ng
       << ",\"avg_latency_ms\":" << s.avg_latency
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
    log_clt("통계 응답 | total=%d ng_rate=%.1f%%", s.total, s.ng_rate);
}

// ── MODEL_LIST ───────────────────────────────────────────────────────

void GuiRouter::handle_model_list(int fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");

    auto models = service_.get_models();

    std::ostringstream items;
    for (size_t i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        if (i > 0) items << ",";
        items << "{\"id\":" << m.id
              << ",\"station_id\":" << m.station_id
              << ",\"model_type\":\"" << escape_json(m.model_type) << "\""
              << ",\"version\":\"" << escape_json(m.version) << "\""
              << ",\"accuracy\":" << m.accuracy
              << ",\"deployed_at\":\"" << escape_json(m.deployed_at) << "\""
              << ",\"is_active\":" << m.is_active
              << "}";
    }

    std::ostringstream os;
    os << "{\"protocol_no\":151"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"count\":" << models.size()
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
    log_clt("모델목록 응답 | %zu건", models.size());
}

// ── RETRAIN ──────────────────────────────────────────────────────────

void GuiRouter::handle_retrain(int fd, const std::string& json) {
    std::string request_id   = extract_str(json, "request_id");
    int station_id           = extract_int(json, "station_id");
    std::string model_type   = extract_str(json, "model_type");
    std::string product_name = extract_str(json, "product_name");
    int image_count          = extract_int(json, "image_count");

    auto result = service_.request_retrain(station_id, model_type, product_name,
                                            image_count, request_id);

    std::ostringstream os;
    os << "{\"protocol_no\":153"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"station_id\":" << station_id
       << ",\"model_type\":\"" << model_type << "\""
       << ",\"message\":\"" << escape_json(result.message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── 유틸리티 ─────────────────────────────────────────────────────────

std::string GuiRouter::get_timestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

std::string GuiRouter::escape_json(const std::string& s) {
    // security 모듈의 통합 구현에 위임 — 제어문자/개행 처리 포함
    return factory::security::escape_json(s);
}

bool GuiRouter::send_json(int fd, const std::string& json_body) {
    return send_json_frame(fd, json_body);
}

std::string GuiRouter::extract_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto fq = json.find('"', colon);
    if (fq == std::string::npos) return "";
    auto lq = json.find('"', fq + 1);
    if (lq == std::string::npos) return "";
    std::string value = json.substr(fq + 1, lq - fq - 1);
    // 입력 문자열 길이 제한 (512자) — 과도한 메모리 사용 차단
    if (value.size() > 512) value.resize(512);
    return value;
}

int GuiRouter::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

} // namespace factory
