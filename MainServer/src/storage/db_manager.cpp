// db_manager.cpp
#include "storage/db_manager.h"
#include "Protocol.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

namespace factory {

DbManager::DbManager(EventBus& bus,
                     const std::string& host,
                     const std::string& user,
                     const std::string& password,
                     const std::string& schema,
                     unsigned int port)
    : event_bus_(bus),
      conn_(nullptr),
      db_host_(host),
      db_user_(user),
      db_password_(password),
      db_schema_(schema),
      db_port_(port) {
}

DbManager::~DbManager() {
    disconnect();
}

void DbManager::register_handlers() {
    event_bus_.subscribe(EventType::DB_WRITE_REQUESTED,
                         [this](const std::any& p) { this->on_db_write(p); });
}

bool DbManager::connect() {
    std::lock_guard<std::mutex> lock(db_mutex_);

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        std::cerr << "[DbManager] mysql_init 실패" << std::endl;
        return false;
    }

    // 자동 재연결 활성화
    my_bool reconnect = 1;
    mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);

    // UTF-8 설정
    mysql_options(conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(conn_,
                            db_host_.c_str(),
                            db_user_.c_str(),
                            db_password_.c_str(),
                            db_schema_.c_str(),
                            db_port_,
                            nullptr, 0)) {
        std::cerr << "[DbManager] DB 연결 실패: " << mysql_error(conn_) << std::endl;
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }

    std::cout << "[DbManager] DB 연결 성공 " << db_host_ << ":" << db_port_
              << "/" << db_schema_ << std::endl;
    return true;
}

void DbManager::disconnect() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
        std::cout << "[DbManager] DB 연결 해제" << std::endl;
    }
}

void DbManager::on_db_write(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    long long inspection_id = 0;
    if (!insert_inspection(ev, inspection_id)) {
        std::cerr << "[DbManager] insert_inspection 실패" << std::endl;
        // NACK 송신 트리거
        AckSendEvent nack{};
        nack.protocol_no   = static_cast<int>(
            ack_no_for(static_cast<ProtocolNo>(ev.protocol_no)));
        nack.inspection_id = ev.inspection_id;
        nack.sender_addr   = ev.sender_addr;
        nack.ack_ok        = false;
        nack.error_message = "db_insert_failed";
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, nack);
        return;
    }

    if (ev.station_id == static_cast<int>(StationId::ASSEMBLY)) {
        if (!insert_assembly(ev, inspection_id)) {
            std::cerr << "[DbManager] insert_assembly 실패" << std::endl;
        }
    }

    // 정상 INSERT 완료 → ACK 송신 트리거
    event_bus_.publish(EventType::DB_WRITE_COMPLETED, ev);
}

bool DbManager::insert_inspection(const InspectionEvent& ev, long long& out_inspection_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!conn_) {
        std::cerr << "[DbManager] DB 미연결 상태" << std::endl;
        return false;
    }

    // raw_json에서 bottle_id, model_id 추출
    int bottle_id = extract_int(ev.raw_json, "bottle_id");
    int model_id  = extract_int(ev.raw_json, "model_id");

    // 이미지 저장 경로 생성 (ImageStorage와 동일한 규칙)
    std::string image_path;
    if (!ev.image_bytes.empty()) {
        std::string yyyymmdd;
        if (ev.timestamp.size() >= 10) {
            yyyymmdd = ev.timestamp.substr(0, 4) +
                       ev.timestamp.substr(5, 2) +
                       ev.timestamp.substr(8, 2);
        }
        image_path = "./storage/station" + std::to_string(ev.station_id) +
                     "/" + yyyymmdd + "/";
    }

    // prepared statement
    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) {
        std::cerr << "[DbManager] mysql_stmt_init 실패" << std::endl;
        return false;
    }

    const char* sql =
        "INSERT INTO inspections "
        "(station_id, bottle_id, model_id, timestamp, result, "
        " confidence, defect_type, image_path, latency_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        std::cerr << "[DbManager] prepare 실패: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 바인드 파라미터 9개
    MYSQL_BIND bind[9];
    std::memset(bind, 0, sizeof(bind));

    // 1) station_id (INT)
    int p_station_id = ev.station_id;
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer      = &p_station_id;

    // 2) bottle_id (INT)
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer      = &bottle_id;

    // 3) model_id (INT)
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer      = &model_id;

    // 4) timestamp (STRING)
    unsigned long ts_len = static_cast<unsigned long>(ev.timestamp.size());
    bind[3].buffer_type   = MYSQL_TYPE_STRING;
    bind[3].buffer        = const_cast<char*>(ev.timestamp.c_str());
    bind[3].buffer_length = ts_len;
    bind[3].length        = &ts_len;

    // 5) result (STRING)
    unsigned long result_len = static_cast<unsigned long>(ev.result.size());
    bind[4].buffer_type   = MYSQL_TYPE_STRING;
    bind[4].buffer        = const_cast<char*>(ev.result.c_str());
    bind[4].buffer_length = result_len;
    bind[4].length        = &result_len;

    // 6) confidence (FLOAT) — score를 confidence로 매핑
    float p_confidence = static_cast<float>(ev.score);
    bind[5].buffer_type = MYSQL_TYPE_FLOAT;
    bind[5].buffer      = &p_confidence;

    // 7) defect_type (STRING, nullable)
    unsigned long defect_len = static_cast<unsigned long>(ev.defect_type.size());
    my_bool defect_null = ev.defect_type.empty() ? 1 : 0;
    bind[6].buffer_type   = MYSQL_TYPE_STRING;
    bind[6].buffer        = const_cast<char*>(ev.defect_type.c_str());
    bind[6].buffer_length = defect_len;
    bind[6].length        = &defect_len;
    bind[6].is_null       = &defect_null;

    // 8) image_path (STRING, nullable)
    unsigned long img_len = static_cast<unsigned long>(image_path.size());
    my_bool img_null = image_path.empty() ? 1 : 0;
    bind[7].buffer_type   = MYSQL_TYPE_STRING;
    bind[7].buffer        = const_cast<char*>(image_path.c_str());
    bind[7].buffer_length = img_len;
    bind[7].length        = &img_len;
    bind[7].is_null       = &img_null;

    // 9) latency_ms (INT)
    int p_latency = ev.latency_ms;
    bind[8].buffer_type = MYSQL_TYPE_LONG;
    bind[8].buffer      = &p_latency;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        std::cerr << "[DbManager] bind 실패: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        std::cerr << "[DbManager] execute 실패: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    out_inspection_id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);

    std::cout << "[DbManager] INSERT inspections id=" << out_inspection_id
              << " station=" << ev.station_id
              << " result=" << ev.result << std::endl;
    return true;
}

bool DbManager::insert_assembly(const InspectionEvent& ev, long long inspection_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!conn_) return false;

    // raw_json에서 조립 검사 상세 필드 추출
    int bottle_id       = extract_int(ev.raw_json, "bottle_id");
    int cap_ok          = extract_int(ev.raw_json, "cap_ok");
    int label_ok        = extract_int(ev.raw_json, "label_ok");
    int fill_ok         = extract_int(ev.raw_json, "fill_ok");
    std::string yolo_detections = extract_str(ev.raw_json, "yolo_detections");
    float patchcore_score = static_cast<float>(extract_double(ev.raw_json, "patchcore_score"));

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;

    const char* sql =
        "INSERT INTO assemblies "
        "(inspection_id, bottle_id, cap_ok, label_ok, fill_ok, "
        " yolo_detections, patchcore_score) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        std::cerr << "[DbManager] assemblies prepare 실패: "
                  << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[7];
    std::memset(bind, 0, sizeof(bind));

    // 1) inspection_id (INT)
    int p_insp_id = static_cast<int>(inspection_id);
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer      = &p_insp_id;

    // 2) bottle_id (INT)
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer      = &bottle_id;

    // 3) cap_ok (TINYINT)
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer      = &cap_ok;

    // 4) label_ok (TINYINT)
    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer      = &label_ok;

    // 5) fill_ok (TINYINT)
    bind[4].buffer_type = MYSQL_TYPE_LONG;
    bind[4].buffer      = &fill_ok;

    // 6) yolo_detections (JSON → STRING)
    unsigned long yolo_len = static_cast<unsigned long>(yolo_detections.size());
    bind[5].buffer_type   = MYSQL_TYPE_STRING;
    bind[5].buffer        = const_cast<char*>(yolo_detections.c_str());
    bind[5].buffer_length = yolo_len;
    bind[5].length        = &yolo_len;

    // 7) patchcore_score (FLOAT)
    bind[6].buffer_type = MYSQL_TYPE_FLOAT;
    bind[6].buffer      = &patchcore_score;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        std::cerr << "[DbManager] assemblies bind 실패: "
                  << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        std::cerr << "[DbManager] assemblies execute 실패: "
                  << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    long long assembly_id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);

    std::cout << "[DbManager] INSERT assemblies id=" << assembly_id
              << " inspection_id=" << inspection_id << std::endl;
    return true;
}

// --- 간이 JSON 추출 ---

int DbManager::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

double DbManager::extract_double(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0.0;
    return std::strtod(json.c_str() + colon + 1, nullptr);
}

std::string DbManager::extract_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    // 값이 문자열이면 "..."
    auto first_quote = json.find('"', colon);
    if (first_quote == std::string::npos) return "";
    auto last_quote = json.find('"', first_quote + 1);
    if (last_quote == std::string::npos) return "";
    return json.substr(first_quote + 1, last_quote - first_quote - 1);
}

} // namespace factory
