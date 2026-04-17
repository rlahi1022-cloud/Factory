// ============================================================================
// gui_tcp_listener.cpp — MFC GUI 전용 TCP 리스너 + 요청 핸들러 (DAO 기반)
// ============================================================================
// DAO를 통해 DB에 접근하며, ConnectionPool에서 커넥션을 빌려 사용한다.
// ============================================================================

#include "session/gui_tcp_listener.h"
#include "session/session_manager.h"
#include "core/logger.h"
#include "storage/password_hash.h"
#include "Protocol.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using socklen_t = int;
  #define CLOSE_SOCK closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCK ::close
#endif

namespace factory {

// ── 생성자/소멸자 ────────────────────────────────────────────────────

GuiTcpListener::GuiTcpListener(EventBus& bus, uint16_t port, ConnectionPool& pool)
    : event_bus_(bus),
      listen_port_(port),
      server_fd_(-1),
      is_running_(false),
      pool_(pool),
      user_dao_(pool),
      model_dao_(pool),
      stats_dao_(pool) {
}

GuiTcpListener::~GuiTcpListener() {
    stop();
}

// ── TCP 서버 ─────────────────────────────────────────────────────────

void GuiTcpListener::start() {
    if (is_running_.exchange(true)) return;

    server_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd_ < 0) {
        log_err_clt("소켓 생성 실패 | GUI 리스너");
        is_running_.store(false);
        return;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct timeval tv{1, 0};
    ::setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(listen_port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_err_clt("바인드 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 8) < 0) {
        log_err_clt("리슨 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    accept_thread_ = std::thread(&GuiTcpListener::run_accept_loop, this);
    log_clt("GUI 리스너 시작 | 포트=%d", listen_port_);
}

void GuiTcpListener::stop() {
    if (!is_running_.exchange(false)) return;
    if (server_fd_ >= 0) {
        CLOSE_SOCK(server_fd_);
        server_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void GuiTcpListener::run_accept_loop() {
    while (is_running_.load()) {
        sockaddr_in client_addr{};
        socklen_t   addr_len = sizeof(client_addr);
        int client_fd = static_cast<int>(
            ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len));
        if (client_fd < 0) continue;

        char ip_buf[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::string remote_addr = std::string(ip_buf) + ":" +
                                  std::to_string(ntohs(client_addr.sin_port));

        std::thread(&GuiTcpListener::handle_client, this, client_fd, remote_addr).detach();
    }
}

void GuiTcpListener::handle_client(int client_fd, const std::string& remote_addr) {
    SessionManager::instance().register_session(client_fd, remote_addr);

    while (is_running_.load()) {
        std::string json_request;
        if (!recv_one_request(client_fd, json_request)) break;
        route_request(client_fd, remote_addr, json_request);
    }

    SessionManager::instance().unregister_session(client_fd);
    CLOSE_SOCK(client_fd);
}

// ── 요청 라우팅 ──────────────────────────────────────────────────────

void GuiTcpListener::route_request(int client_fd, const std::string& remote_addr,
                                   const std::string& json_request) {
    int protocol_no = extract_int(json_request, "protocol_no");

    switch (protocol_no) {
        case static_cast<int>(ProtocolNo::LOGIN_REQ):
            handle_login_req(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::LOGOUT_REQ):
            handle_logout_req(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::REGISTER_REQ):
            handle_register_req(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::INSPECT_HISTORY_REQ):
            handle_inspect_history_req(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::STATS_REQ):
            handle_stats_req(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::MODEL_LIST_REQ):
            handle_model_list_req(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::RETRAIN_REQ):
            handle_retrain_req(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::EXT_ACK):
            break;
        default:
            log_clt("미처리 프로토콜 | no=%d ip=%s", protocol_no, remote_addr.c_str());
            break;
    }
}

// ── LOGIN_REQ(100) ───────────────────────────────────────────────────

void GuiTcpListener::handle_login_req(int client_fd, const std::string& json) {
    std::string username   = extract_str(json, "username");
    std::string password   = extract_str(json, "password");
    std::string request_id = extract_str(json, "request_id");

    log_clt("로그인 요청 | 사용자=%s", username.c_str());

    auto user = user_dao_.find_by_username(username);
    bool success = user.found && PasswordHash::verify(password, user.password_hash);

    if (success) {
        SessionManager::instance().set_client_info(client_fd, username, 0);
        user_dao_.update_last_login(username);
    }

    std::ostringstream os;
    os << "{\"protocol_no\":101"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"success\":" << (success ? "true" : "false")
       << ",\"username\":\"" << username << "\""
       << ",\"role\":\"" << user.role << "\""
       << ",\"employee_id\":\"" << user.employee_id << "\""
       << ",\"message\":\"" << (success ? "로그인 성공" : "인증 실패") << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    if (success)
        log_clt("로그인 성공 | 사용자=%s 권한=%s", username.c_str(), user.role.c_str());
    else
        log_err_clt("로그인 실패 | 사용자=%s", username.c_str());
}

// ── REGISTER_REQ(104) ────────────────────────────────────────────────

void GuiTcpListener::handle_register_req(int client_fd, const std::string& json) {
    std::string username    = extract_str(json, "username");
    std::string password    = extract_str(json, "password");
    std::string employee_id = extract_str(json, "employee_id");
    std::string role        = extract_str(json, "role");
    std::string request_id  = extract_str(json, "request_id");

    log_clt("회원가입 요청 | 사용자=%s 사원ID=%s 권한=%s",
            username.c_str(), employee_id.c_str(), role.c_str());

    bool success = false;
    std::string message;

    if (user_dao_.exists(username)) {
        message = "이미 존재하는 사용자입니다.";
    } else if (user_dao_.insert(employee_id, username, password, role)) {
        success = true;
        message = "회원가입 성공";
    } else {
        message = "DB 오류";
    }

    std::ostringstream os;
    os << "{\"protocol_no\":105"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"success\":" << (success ? "true" : "false")
       << ",\"message\":\"" << escape_json(message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    if (success)
        log_clt("회원가입 성공 | 사용자=%s", username.c_str());
    else
        log_err_clt("회원가입 실패 | 사용자=%s 사유=%s", username.c_str(), message.c_str());
}

// ── LOGOUT_REQ(102) ──────────────────────────────────────────────────

void GuiTcpListener::handle_logout_req(int client_fd, const std::string& json) {
    std::string username = extract_str(json, "username");

    std::ostringstream os;
    os << "{\"protocol_no\":103"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"success\":true"
       << ",\"message\":\"로그아웃 완료\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    log_clt("로그아웃 | 사용자=%s", username.c_str());
}

// ── INSPECT_HISTORY_REQ(114) ─────────────────────────────────────────

void GuiTcpListener::handle_inspect_history_req(int client_fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");
    int limit              = extract_int(json, "limit");

    auto records = stats_dao_.get_history(station_filter, date_from, date_to, limit);

    std::ostringstream items;
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        if (i > 0) items << ",";
        items << "{\"id\":" << r.id
              << ",\"station_id\":" << r.station_id
              << ",\"timestamp\":\"" << r.timestamp << "\""
              << ",\"result\":\"" << r.result << "\""
              << ",\"confidence\":" << r.confidence
              << ",\"defect_type\":\"" << r.defect_type << "\""
              << ",\"image_path\":\"" << r.image_path << "\""
              << ",\"latency_ms\":" << r.latency_ms
              << "}";
    }

    std::ostringstream os;
    os << "{\"protocol_no\":115"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"count\":" << records.size()
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    log_clt("검사이력 응답 | %zu건", records.size());
}

// ── STATS_REQ(130) ───────────────────────────────────────────────────

void GuiTcpListener::handle_stats_req(int client_fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");

    auto s = stats_dao_.get_stats(station_filter, date_from, date_to);

    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{\"protocol_no\":131"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"total\":" << s.total
       << ",\"ok_count\":" << s.ok_count
       << ",\"ng_count\":" << s.ng_count
       << ",\"ng_rate\":" << s.ng_rate
       << ",\"s1_ok\":" << s.s1_ok << ",\"s1_ng\":" << s.s1_ng
       << ",\"s2_ok\":" << s.s2_ok << ",\"s2_ng\":" << s.s2_ng
       << ",\"avg_latency_ms\":" << s.avg_latency
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    log_clt("통계 응답 | total=%d ng_rate=%.1f%%", s.total, s.ng_rate);
}

// ── MODEL_LIST_REQ(150) ──────────────────────────────────────────────

void GuiTcpListener::handle_model_list_req(int client_fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");

    auto models = model_dao_.list_all();

    std::ostringstream items;
    for (size_t i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        if (i > 0) items << ",";
        items << "{\"id\":" << m.id
              << ",\"station_id\":" << m.station_id
              << ",\"model_type\":\"" << m.model_type << "\""
              << ",\"version\":\"" << m.version << "\""
              << ",\"accuracy\":" << m.accuracy
              << ",\"deployed_at\":\"" << m.deployed_at << "\""
              << ",\"is_active\":" << m.is_active
              << "}";
    }

    std::ostringstream os;
    os << "{\"protocol_no\":151"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"count\":" << models.size()
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    log_clt("모델목록 응답 | %zu건", models.size());
}

// ── RETRAIN_REQ(152) ─────────────────────────────────────────────────

void GuiTcpListener::handle_retrain_req(int client_fd, const std::string& json) {
    std::string request_id   = extract_str(json, "request_id");
    int station_id           = extract_int(json, "station_id");
    std::string model_type   = extract_str(json, "model_type");
    std::string product_name = extract_str(json, "product_name");
    int image_count          = extract_int(json, "image_count");

    log_train("재학습 요청 접수 | 스테이션=%d 모델=%s 이미지=%d건",
              station_id, model_type.c_str(), image_count);

    bool train_sent = false;
    const char* train_host = "10.10.10.130";
    uint16_t    train_port = 9100;

    int train_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (train_fd >= 0) {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(train_port);
        inet_pton(AF_INET, train_host, &addr.sin_addr);

        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(train_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

        if (::connect(train_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            std::ostringstream train_os;
            train_os << "{\"protocol_no\":1100"
                     << ",\"protocol_version\":\"1.0\""
                     << ",\"request_id\":\"" << request_id << "\""
                     << ",\"station_id\":" << station_id
                     << ",\"model_type\":\"" << model_type << "\""
                     << ",\"product_name\":\"" << product_name << "\""
                     << ",\"image_count\":" << image_count
                     << ",\"timestamp\":\"" << get_timestamp() << "\"}";

            train_sent = send_json(train_fd, train_os.str());
            if (train_sent)
                log_train("TRAIN_START_REQ → 학습서버 전송 성공");
            else
                log_err_train("TRAIN_START_REQ → 학습서버 전송 실패");
        } else {
            log_err_train("학습서버 연결 실패 | %s:%d", train_host, train_port);
        }
        ::close(train_fd);
    }

    std::ostringstream os;
    os << "{\"protocol_no\":153"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"success\":" << (train_sent ? "true" : "false")
       << ",\"station_id\":" << station_id
       << ",\"model_type\":\"" << model_type << "\""
       << ",\"message\":\"" << (train_sent ? "재학습 요청 전달 완료" : "학습서버 연결 실패") << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
}

// ── 유틸리티 ─────────────────────────────────────────────────────────

std::string GuiTcpListener::get_timestamp() {
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

std::string GuiTcpListener::escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

bool GuiTcpListener::send_json(int fd, const std::string& json_body) {
    uint32_t json_size = static_cast<uint32_t>(json_body.size());
    uint8_t header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),
        static_cast<uint8_t>((json_size >> 16) & 0xFF),
        static_cast<uint8_t>((json_size >>  8) & 0xFF),
        static_cast<uint8_t>( json_size        & 0xFF),
    };
    int sent_h = static_cast<int>(::send(fd, reinterpret_cast<const char*>(header), 4, 0));
    int sent_b = static_cast<int>(::send(fd, json_body.c_str(),
                                         static_cast<int>(json_body.size()), 0));
    return (sent_h == 4 && sent_b == static_cast<int>(json_body.size()));
}

std::string GuiTcpListener::extract_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto fq = json.find('"', colon);
    if (fq == std::string::npos) return "";
    auto lq = json.find('"', fq + 1);
    if (lq == std::string::npos) return "";
    return json.substr(fq + 1, lq - fq - 1);
}

int GuiTcpListener::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

// ── 패킷 수신 ────────────────────────────────────────────────────────

static bool recv_n(int fd, void* buf, std::size_t n) {
    std::size_t total = 0;
    auto* p = static_cast<char*>(buf);
    while (total < n) {
        int got = static_cast<int>(::recv(fd, p + total, static_cast<int>(n - total), 0));
        if (got <= 0) return false;
        total += static_cast<std::size_t>(got);
    }
    return true;
}

bool GuiTcpListener::recv_one_request(int client_fd, std::string& out_json) {
    uint8_t header[HEADER_SIZE];
    if (!recv_n(client_fd, header, HEADER_SIZE)) return false;

    uint32_t json_size = (uint32_t)header[0] << 24 |
                         (uint32_t)header[1] << 16 |
                         (uint32_t)header[2] << 8  |
                         (uint32_t)header[3];

    if (json_size == 0 || json_size > 64 * 1024) return false;

    out_json.assign(json_size, '\0');
    if (!recv_n(client_fd, out_json.data(), json_size)) return false;
    return true;
}

} // namespace factory
