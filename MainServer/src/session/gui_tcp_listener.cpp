// gui_tcp_listener.cpp
// MFC GUI 클라이언트 전용 TCP 리스너 + 요청 핸들러
// LOGIN, LOGOUT, INSPECT_HISTORY, STATS, MODEL_LIST, RETRAIN 처리

#include "session/gui_tcp_listener.h"
#include "session/session_manager.h"
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

GuiTcpListener::GuiTcpListener(EventBus& bus, uint16_t port,
                               const std::string& db_host,
                               const std::string& db_user,
                               const std::string& db_password,
                               const std::string& db_schema,
                               unsigned int db_port)
    : event_bus_(bus),
      listen_port_(port),
      server_fd_(-1),
      is_running_(false),
      db_conn_(nullptr),
      db_host_(db_host),
      db_user_(db_user),
      db_password_(db_password),
      db_schema_(db_schema),
      db_port_(db_port) {
}

GuiTcpListener::~GuiTcpListener() {
    stop();
    db_disconnect();
}

// ── DB 연결 ──────────────────────────────────────────────────────────

bool GuiTcpListener::db_connect() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    db_conn_ = mysql_init(nullptr);
    if (!db_conn_) return false;

    my_bool reconnect = 1;
    mysql_options(db_conn_, MYSQL_OPT_RECONNECT, &reconnect);
    mysql_options(db_conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(db_conn_, db_host_.c_str(), db_user_.c_str(),
                            db_password_.c_str(), db_schema_.c_str(),
                            db_port_, nullptr, 0)) {
        std::cerr << "[GuiTcpListener] DB 연결 실패: " << mysql_error(db_conn_) << std::endl;
        mysql_close(db_conn_);
        db_conn_ = nullptr;
        return false;
    }
    std::cout << "[GuiTcpListener] GUI용 DB 연결 성공" << std::endl;
    return true;
}

void GuiTcpListener::db_disconnect() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_conn_) {
        mysql_close(db_conn_);
        db_conn_ = nullptr;
    }
}

// ── TCP 서버 ─────────────────────────────────────────────────────────

void GuiTcpListener::start() {
    if (is_running_.exchange(true)) return;

    db_connect();

    server_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd_ < 0) {
        std::cerr << "[GuiTcpListener] socket() failed" << std::endl;
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
        std::cerr << "[GuiTcpListener] bind() failed on port " << listen_port_ << std::endl;
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 8) < 0) {
        std::cerr << "[GuiTcpListener] listen() failed" << std::endl;
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    accept_thread_ = std::thread(&GuiTcpListener::run_accept_loop, this);
    std::cout << "[GuiTcpListener] listening on :" << listen_port_ << std::endl;
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
            handle_login_req(client_fd, json_request);
            break;
        case static_cast<int>(ProtocolNo::LOGOUT_REQ):
            handle_logout_req(client_fd, json_request);
            break;
        case static_cast<int>(ProtocolNo::INSPECT_HISTORY_REQ):
            handle_inspect_history_req(client_fd, json_request);
            break;
        case static_cast<int>(ProtocolNo::STATS_REQ):
            handle_stats_req(client_fd, json_request);
            break;
        case static_cast<int>(ProtocolNo::MODEL_LIST_REQ):
            handle_model_list_req(client_fd, json_request);
            break;
        case static_cast<int>(ProtocolNo::RETRAIN_REQ):
            handle_retrain_req(client_fd, json_request);
            break;
        case static_cast<int>(ProtocolNo::REGISTER_REQ):
            handle_register_req(client_fd, json_request);
            break;
        case static_cast<int>(ProtocolNo::EXT_ACK):
            break; // heartbeat 무시
        default:
            std::cout << "[GuiTcpListener] 미처리 protocol_no=" << protocol_no
                      << " from " << remote_addr << std::endl;
            break;
    }
}

// ── 1. LOGIN_REQ(100) → LOGIN_RES(101) ──────────────────────────────

void GuiTcpListener::handle_login_req(int client_fd, const std::string& json) {
    // 디버그: 수신된 JSON 전체 출력
    std::cout << "[GuiTcpListener] LOGIN JSON: " << json.substr(0, 200) << std::endl;

    std::string username   = extract_str(json, "username");
    std::string password   = extract_str(json, "password");
    std::string request_id = extract_str(json, "request_id");

    std::cout << "[GuiTcpListener] 파싱 결과 user=[" << username
              << "] pass=[" << password << "]" << std::endl;

    bool success = false;
    std::string role, employee_id;

    // DB users 테이블 조회
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (db_conn_) {
            // SQL Injection 방지
            char esc_user[256];
            mysql_real_escape_string(db_conn_, esc_user, username.c_str(), username.size());
            std::string sql = "SELECT employee_id, role, password_hash FROM users WHERE username='"
                              + std::string(esc_user) + "' LIMIT 1";
            if (mysql_query(db_conn_, sql.c_str()) == 0) {
                MYSQL_RES* res = mysql_store_result(db_conn_);
                if (res) {
                    MYSQL_ROW row = mysql_fetch_row(res);
                    if (row) {
                        employee_id = row[0] ? row[0] : "";
                        role        = row[1] ? row[1] : "";
                        std::string db_password = row[2] ? row[2] : "";
                        // TODO: bcrypt 검증. 현재는 평문 비교
                        if (db_password == password) {
                            success = true;
                        }
                    }
                    mysql_free_result(res);
                }
            }
        }
    }

    // DB 조회 실패 시 인증 실패 처리 (하드코딩 폴백 없음)

    if (success) {
        SessionManager::instance().set_client_info(client_fd, username, 0);
        // last_login_at 업데이트
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (db_conn_) {
            char esc_user2[256];
            mysql_real_escape_string(db_conn_, esc_user2, username.c_str(), username.size());
            std::string sql = "UPDATE users SET last_login_at=NOW() WHERE username='"
                              + std::string(esc_user2) + "'";
            mysql_query(db_conn_, sql.c_str());
        }
    }

    std::ostringstream os;
    os << "{\"protocol_no\":101"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"success\":" << (success ? "true" : "false")
       << ",\"username\":\"" << username << "\""
       << ",\"role\":\"" << role << "\""
       << ",\"employee_id\":\"" << employee_id << "\""
       << ",\"message\":\"" << (success ? "로그인 성공" : "인증 실패") << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    std::cout << "[GuiTcpListener] LOGIN " << (success ? "성공" : "실패")
              << " user=" << username << std::endl;
}

// ── REGISTER_REQ(104) → REGISTER_RES(105) ───────────────────────────

void GuiTcpListener::handle_register_req(int client_fd, const std::string& json) {
    std::string username    = extract_str(json, "username");
    std::string password    = extract_str(json, "password");
    std::string employee_id = extract_str(json, "employee_id");
    std::string role        = extract_str(json, "role");
    std::string request_id  = extract_str(json, "request_id");

    std::cout << "[GuiTcpListener] REGISTER_REQ user=[" << username
              << "] emp=[" << employee_id << "] role=[" << role << "]" << std::endl;

    bool success = false;
    std::string message;

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!db_conn_) {
            message = "DB 연결 실패";
        } else {
            // SQL Injection 방지: mysql_real_escape_string 사용
            char esc_user[256], esc_pass[512], esc_emp[128], esc_role[64];
            mysql_real_escape_string(db_conn_, esc_user, username.c_str(), username.size());
            mysql_real_escape_string(db_conn_, esc_pass, password.c_str(), password.size());
            mysql_real_escape_string(db_conn_, esc_emp,  employee_id.c_str(), employee_id.size());
            mysql_real_escape_string(db_conn_, esc_role, role.c_str(), role.size());

            // 중복 사용자 확인
            std::string check_sql = "SELECT id FROM users WHERE username='"
                                    + std::string(esc_user) + "' LIMIT 1";
            if (mysql_query(db_conn_, check_sql.c_str()) == 0) {
                MYSQL_RES* res = mysql_store_result(db_conn_);
                if (res) {
                    if (mysql_fetch_row(res)) {
                        message = "이미 존재하는 사용자입니다.";
                    }
                    mysql_free_result(res);
                }
            }

            if (message.empty()) {
                // INSERT — TODO: bcrypt 해시 적용
                std::string insert_sql =
                    "INSERT INTO users (employee_id, username, password_hash, role, created_at) "
                    "VALUES ('"
                    + std::string(esc_emp) + "','"
                    + std::string(esc_user) + "','"
                    + std::string(esc_pass) + "','"
                    + std::string(esc_role) + "',NOW())";

                if (mysql_query(db_conn_, insert_sql.c_str()) == 0) {
                    success = true;
                    message = "회원가입 성공";
                } else {
                    message = "DB 오류: ";
                    message += mysql_error(db_conn_);
                }
            }
        }
    }

    std::ostringstream os;
    os << "{\"protocol_no\":105"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"success\":" << (success ? "true" : "false")
       << ",\"message\":\"" << escape_json(message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    std::cout << "[GuiTcpListener] REGISTER " << (success ? "성공" : "실패")
              << " user=" << username << " msg=" << message << std::endl;
}

// ── 2. LOGOUT_REQ(102) → LOGOUT_RES(103) ────────────────────────────

void GuiTcpListener::handle_logout_req(int client_fd, const std::string& json) {
    std::string username = extract_str(json, "username");

    std::ostringstream os;
    os << "{\"protocol_no\":103"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"success\":true"
       << ",\"message\":\"로그아웃 완료\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    std::cout << "[GuiTcpListener] LOGOUT user=" << username << std::endl;
}

// ── 3. INSPECT_HISTORY_REQ(114) → INSPECT_HISTORY_RES(115) ──────────

void GuiTcpListener::handle_inspect_history_req(int client_fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");
    int limit              = extract_int(json, "limit");
    if (limit <= 0 || limit > 500) limit = 100;

    std::ostringstream items;
    int count = 0;

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (db_conn_) {
            std::ostringstream sql;
            sql << "SELECT id, station_id, timestamp, result, confidence, "
                << "defect_type, image_path, latency_ms FROM inspections WHERE 1=1";
            if (station_filter > 0)
                sql << " AND station_id=" << station_filter;
            if (!date_from.empty())
                sql << " AND timestamp>='" << date_from << "'";
            if (!date_to.empty())
                sql << " AND timestamp<='" << date_to << " 23:59:59'";
            sql << " ORDER BY id DESC LIMIT " << limit;

            if (mysql_query(db_conn_, sql.str().c_str()) == 0) {
                MYSQL_RES* res = mysql_store_result(db_conn_);
                if (res) {
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(res))) {
                        if (count > 0) items << ",";
                        items << "{\"id\":" << (row[0] ? row[0] : "0")
                              << ",\"station_id\":" << (row[1] ? row[1] : "0")
                              << ",\"timestamp\":\"" << (row[2] ? row[2] : "") << "\""
                              << ",\"result\":\"" << (row[3] ? row[3] : "") << "\""
                              << ",\"confidence\":" << (row[4] ? row[4] : "0")
                              << ",\"defect_type\":\"" << (row[5] ? row[5] : "") << "\""
                              << ",\"image_path\":\"" << (row[6] ? row[6] : "") << "\""
                              << ",\"latency_ms\":" << (row[7] ? row[7] : "0")
                              << "}";
                        count++;
                    }
                    mysql_free_result(res);
                }
            }
        }
    }

    std::ostringstream os;
    os << "{\"protocol_no\":115"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"count\":" << count
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    std::cout << "[GuiTcpListener] INSPECT_HISTORY 응답 " << count << "건" << std::endl;
}

// ── 4. STATS_REQ(130) → STATS_RES(131) ──────────────────────────────

void GuiTcpListener::handle_stats_req(int client_fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");

    int total = 0, ok_count = 0, ng_count = 0;
    int s1_ok = 0, s1_ng = 0, s2_ok = 0, s2_ng = 0;
    double avg_latency = 0.0;

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (db_conn_) {
            std::ostringstream sql;
            sql << "SELECT station_id, result, COUNT(*), AVG(latency_ms) "
                << "FROM inspections WHERE 1=1";
            if (station_filter > 0)
                sql << " AND station_id=" << station_filter;
            if (!date_from.empty())
                sql << " AND timestamp>='" << date_from << "'";
            if (!date_to.empty())
                sql << " AND timestamp<='" << date_to << " 23:59:59'";
            sql << " GROUP BY station_id, result";

            if (mysql_query(db_conn_, sql.str().c_str()) == 0) {
                MYSQL_RES* res = mysql_store_result(db_conn_);
                if (res) {
                    MYSQL_ROW row;
                    double lat_sum = 0; int lat_cnt = 0;
                    while ((row = mysql_fetch_row(res))) {
                        int sid = row[0] ? std::atoi(row[0]) : 0;
                        std::string result = row[1] ? row[1] : "";
                        int cnt = row[2] ? std::atoi(row[2]) : 0;
                        double lat = row[3] ? std::atof(row[3]) : 0;

                        total += cnt;
                        lat_sum += lat * cnt; lat_cnt += cnt;

                        if (result == "ok") {
                            ok_count += cnt;
                            if (sid == 1) s1_ok += cnt; else s2_ok += cnt;
                        } else {
                            ng_count += cnt;
                            if (sid == 1) s1_ng += cnt; else s2_ng += cnt;
                        }
                    }
                    if (lat_cnt > 0) avg_latency = lat_sum / lat_cnt;
                    mysql_free_result(res);
                }
            }
        }
    }

    double ng_rate = total > 0 ? (100.0 * ng_count / total) : 0.0;

    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{\"protocol_no\":131"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"total\":" << total
       << ",\"ok_count\":" << ok_count
       << ",\"ng_count\":" << ng_count
       << ",\"ng_rate\":" << ng_rate
       << ",\"s1_ok\":" << s1_ok << ",\"s1_ng\":" << s1_ng
       << ",\"s2_ok\":" << s2_ok << ",\"s2_ng\":" << s2_ng
       << ",\"avg_latency_ms\":" << avg_latency
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    std::cout << "[GuiTcpListener] STATS 응답 total=" << total
              << " ng_rate=" << ng_rate << "%" << std::endl;
}

// ── 5. MODEL_LIST_REQ(150) → MODEL_LIST_RES(151) ────────────────────

void GuiTcpListener::handle_model_list_req(int client_fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");

    std::ostringstream items;
    int count = 0;

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (db_conn_) {
            const char* sql =
                "SELECT id, station_id, model_type, version, accuracy, "
                "deployed_at, is_active FROM models ORDER BY id DESC";

            if (mysql_query(db_conn_, sql) == 0) {
                MYSQL_RES* res = mysql_store_result(db_conn_);
                if (res) {
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(res))) {
                        if (count > 0) items << ",";
                        items << "{\"id\":" << (row[0] ? row[0] : "0")
                              << ",\"station_id\":" << (row[1] ? row[1] : "0")
                              << ",\"model_type\":\"" << (row[2] ? row[2] : "") << "\""
                              << ",\"version\":\"" << (row[3] ? row[3] : "") << "\""
                              << ",\"accuracy\":" << (row[4] ? row[4] : "0")
                              << ",\"deployed_at\":\"" << (row[5] ? row[5] : "") << "\""
                              << ",\"is_active\":" << (row[6] ? row[6] : "0")
                              << "}";
                        count++;
                    }
                    mysql_free_result(res);
                }
            }
        }
    }

    std::ostringstream os;
    os << "{\"protocol_no\":151"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"count\":" << count
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(client_fd, os.str());
    std::cout << "[GuiTcpListener] MODEL_LIST 응답 " << count << "건" << std::endl;
}

// ── 6. RETRAIN_REQ(152) → RETRAIN_RES(153) ──────────────────────────

void GuiTcpListener::handle_retrain_req(int client_fd, const std::string& json) {
    std::string request_id   = extract_str(json, "request_id");
    int station_id           = extract_int(json, "station_id");
    std::string model_type   = extract_str(json, "model_type");
    std::string product_name = extract_str(json, "product_name");
    int image_count          = extract_int(json, "image_count");

    // TODO: 학습서버(9100)에 TRAIN_START_REQ(1100) TCP 전달
    // 현재는 요청 접수 응답만 전송
    std::cout << "[GuiTcpListener] RETRAIN 요청 접수 station=" << station_id
              << " model=" << model_type << " images=" << image_count << std::endl;

    std::ostringstream os;
    os << "{\"protocol_no\":153"
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"success\":true"
       << ",\"station_id\":" << station_id
       << ",\"model_type\":\"" << model_type << "\""
       << ",\"message\":\"재학습 요청 접수\""
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
