// ============================================================================
// dao.cpp — 테이블별 DAO 구현
// ============================================================================
// ConnectionPool에서 PooledConnection(RAII)으로 커넥션을 획득하여 사용.
// 스코프를 벗어나면 자동으로 풀에 반납된다.
// ============================================================================
#include "storage/dao.h"
#include "storage/password_hash.h"
#include "core/logger.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace factory {

// ============================================================================
// InspectionDao
// ============================================================================

long long InspectionDao::insert(const InspectionEvent& ev) {
    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    // 이미지 경로 구성
    std::string image_path;
    if (!ev.image_bytes.empty() && ev.timestamp.size() >= 10) {
        std::string yyyymmdd = ev.timestamp.substr(0, 4) +
                               ev.timestamp.substr(5, 2) +
                               ev.timestamp.substr(8, 2);
        image_path = "./storage/station" + std::to_string(ev.station_id) +
                     "/" + yyyymmdd + "/";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) { log_err_db("InspectionDao stmt_init 실패"); return -1; }

    const char* sql =
        "INSERT INTO inspections "
        "(station_id, timestamp, result, confidence, defect_type, image_path, latency_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("InspectionDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[7];
    std::memset(bind, 0, sizeof(bind));

    int p_station_id = ev.station_id;
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &p_station_id;

    unsigned long ts_len = static_cast<unsigned long>(ev.timestamp.size());
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(ev.timestamp.c_str());
    bind[1].buffer_length = ts_len;
    bind[1].length = &ts_len;

    unsigned long result_len = static_cast<unsigned long>(ev.result.size());
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char*>(ev.result.c_str());
    bind[2].buffer_length = result_len;
    bind[2].length = &result_len;

    float p_confidence = static_cast<float>(ev.score);
    bind[3].buffer_type = MYSQL_TYPE_FLOAT;
    bind[3].buffer = &p_confidence;

    unsigned long defect_len = static_cast<unsigned long>(ev.defect_type.size());
    my_bool defect_null = ev.defect_type.empty() ? 1 : 0;
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = const_cast<char*>(ev.defect_type.c_str());
    bind[4].buffer_length = defect_len;
    bind[4].length = &defect_len;
    bind[4].is_null = &defect_null;

    unsigned long img_len = static_cast<unsigned long>(image_path.size());
    my_bool img_null = image_path.empty() ? 1 : 0;
    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer = const_cast<char*>(image_path.c_str());
    bind[5].buffer_length = img_len;
    bind[5].length = &img_len;
    bind[5].is_null = &img_null;

    int p_latency = ev.latency_ms;
    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = &p_latency;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_err_db("InspectionDao bind 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_err_db("InspectionDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);

    log_db("INSERT inspections | id=%lld station=%d result=%s", id, ev.station_id, ev.result.c_str());
    return id;
}

// ============================================================================
// AssemblyDao
// ============================================================================

int AssemblyDao::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

double AssemblyDao::extract_double(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0.0;
    return std::strtod(json.c_str() + colon + 1, nullptr);
}

std::string AssemblyDao::extract_array(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "[]";
    auto bracket = json.find('[', pos);
    if (bracket == std::string::npos) return "[]";
    int depth = 0;
    for (std::size_t i = bracket; i < json.size(); ++i) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') { depth--; if (depth == 0) return json.substr(bracket, i - bracket + 1); }
    }
    return "[]";
}

long long AssemblyDao::insert(const InspectionEvent& ev, long long inspection_id) {
    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    int cap_ok = extract_int(ev.raw_json, "cap_ok");
    int label_ok = extract_int(ev.raw_json, "label_ok");
    int fill_ok = extract_int(ev.raw_json, "fill_ok");
    float patchcore_score = static_cast<float>(extract_double(ev.raw_json, "patchcore_score"));
    std::string yolo_detections = extract_array(ev.raw_json, "detections");

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    const char* sql =
        "INSERT INTO assemblies "
        "(inspection_id, cap_ok, label_ok, fill_ok, yolo_detections, patchcore_score) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("AssemblyDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[6];
    std::memset(bind, 0, sizeof(bind));

    int p_insp_id = static_cast<int>(inspection_id);
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &p_insp_id;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &cap_ok;

    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &label_ok;

    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = &fill_ok;

    unsigned long yolo_len = static_cast<unsigned long>(yolo_detections.size());
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = const_cast<char*>(yolo_detections.c_str());
    bind[4].buffer_length = yolo_len;
    bind[4].length = &yolo_len;

    bind[5].buffer_type = MYSQL_TYPE_FLOAT;
    bind[5].buffer = &patchcore_score;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("AssemblyDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    log_db("INSERT assemblies | id=%lld inspection_id=%lld", id, inspection_id);
    return id;
}

// ============================================================================
// ModelDao
// ============================================================================

bool ModelDao::insert(const TrainCompleteEvent& ev) {
    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    const char* sql =
        "INSERT INTO models (station_id, model_type, version, accuracy, model_path, deployed_at, is_active) "
        "VALUES (?, ?, ?, ?, ?, NOW(), 1)";

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        log_err_db("ModelDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    int station_id = ev.station_id;
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &station_id;

    unsigned long mt_len = static_cast<unsigned long>(ev.model_type.size());
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(ev.model_type.c_str());
    bind[1].buffer_length = mt_len;
    bind[1].length = &mt_len;

    unsigned long ver_len = static_cast<unsigned long>(ev.version.size());
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char*>(ev.version.c_str());
    bind[2].buffer_length = ver_len;
    bind[2].length = &ver_len;

    double accuracy = ev.accuracy;
    bind[3].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[3].buffer = &accuracy;

    unsigned long path_len = static_cast<unsigned long>(ev.model_path.size());
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = const_cast<char*>(ev.model_path.c_str());
    bind[4].buffer_length = path_len;
    bind[4].length = &path_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("ModelDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    log_db("INSERT models | id=%lld 스테이션=%d 모델=%s 버전=%s",
           id, ev.station_id, ev.model_type.c_str(), ev.version.c_str());
    return true;
}

std::vector<ModelDao::ModelInfo> ModelDao::list_all() {
    std::vector<ModelInfo> result;
    PooledConnection conn(pool_);
    if (!conn.get()) return result;

    const char* sql =
        "SELECT id, station_id, model_type, version, accuracy, deployed_at, is_active "
        "FROM models ORDER BY id DESC";

    if (mysql_query(conn, sql) != 0) return result;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return result;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ModelInfo m;
        m.id = row[0] ? std::atoi(row[0]) : 0;
        m.station_id = row[1] ? std::atoi(row[1]) : 0;
        m.model_type = row[2] ? row[2] : "";
        m.version = row[3] ? row[3] : "";
        m.accuracy = row[4] ? std::atof(row[4]) : 0;
        m.deployed_at = row[5] ? row[5] : "";
        m.is_active = row[6] ? std::atoi(row[6]) : 0;
        result.push_back(m);
    }
    mysql_free_result(res);
    return result;
}

// ============================================================================
// UserDao
// ============================================================================

UserDao::UserInfo UserDao::find_by_username(const std::string& username) {
    UserInfo info;
    PooledConnection conn(pool_);
    if (!conn.get()) return info;

    char esc[256];
    mysql_real_escape_string(conn, esc, username.c_str(), username.size());
    std::string sql = "SELECT employee_id, role, password_hash FROM users WHERE username='"
                      + std::string(esc) + "' LIMIT 1";

    if (mysql_query(conn, sql.c_str()) != 0) return info;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return info;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        info.employee_id = row[0] ? row[0] : "";
        info.role = row[1] ? row[1] : "";
        info.password_hash = row[2] ? row[2] : "";
        info.found = true;
    }
    mysql_free_result(res);
    return info;
}

bool UserDao::exists(const std::string& username) {
    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    char esc[256];
    mysql_real_escape_string(conn, esc, username.c_str(), username.size());
    std::string sql = "SELECT id FROM users WHERE username='" + std::string(esc) + "' LIMIT 1";

    if (mysql_query(conn, sql.c_str()) != 0) return false;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return false;

    bool found = (mysql_fetch_row(res) != nullptr);
    mysql_free_result(res);
    return found;
}

bool UserDao::insert(const std::string& employee_id, const std::string& username,
                     const std::string& password, const std::string& role) {
    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    // 평문 비밀번호를 bcrypt 해시로 변환
    std::string hashed = PasswordHash::hash(password);
    if (hashed.empty()) {
        log_err_db("비밀번호 해시 생성 실패 | 사용자=%s", username.c_str());
        return false;
    }

    char esc_emp[128], esc_user[256], esc_pass[512], esc_role[64];
    mysql_real_escape_string(conn, esc_emp, employee_id.c_str(), employee_id.size());
    mysql_real_escape_string(conn, esc_user, username.c_str(), username.size());
    mysql_real_escape_string(conn, esc_pass, hashed.c_str(), hashed.size());
    mysql_real_escape_string(conn, esc_role, role.c_str(), role.size());

    std::string sql =
        "INSERT INTO users (employee_id, username, password_hash, role, created_at) VALUES ('"
        + std::string(esc_emp) + "','" + std::string(esc_user) + "','"
        + std::string(esc_pass) + "','" + std::string(esc_role) + "',NOW())";

    return (mysql_query(conn, sql.c_str()) == 0);
}

void UserDao::update_last_login(const std::string& username) {
    PooledConnection conn(pool_);
    if (!conn.get()) return;

    char esc[256];
    mysql_real_escape_string(conn, esc, username.c_str(), username.size());
    std::string sql = "UPDATE users SET last_login_at=NOW() WHERE username='"
                      + std::string(esc) + "'";
    mysql_query(conn, sql.c_str());
}

// ============================================================================
// StatsDao
// ============================================================================

StatsDao::StatsResult StatsDao::get_stats(int station_filter,
                                           const std::string& date_from,
                                           const std::string& date_to) {
    StatsResult r;
    PooledConnection conn(pool_);
    if (!conn.get()) return r;

    std::ostringstream sql;
    sql << "SELECT station_id, result, COUNT(*), AVG(latency_ms) "
        << "FROM inspections WHERE 1=1";
    if (station_filter > 0) sql << " AND station_id=" << station_filter;
    if (!date_from.empty()) sql << " AND timestamp>='" << date_from << "'";
    if (!date_to.empty()) sql << " AND timestamp<='" << date_to << " 23:59:59'";
    sql << " GROUP BY station_id, result";

    if (mysql_query(conn, sql.str().c_str()) != 0) return r;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return r;

    MYSQL_ROW row;
    double lat_sum = 0; int lat_cnt = 0;
    while ((row = mysql_fetch_row(res))) {
        int sid = row[0] ? std::atoi(row[0]) : 0;
        std::string result = row[1] ? row[1] : "";
        int cnt = row[2] ? std::atoi(row[2]) : 0;
        double lat = row[3] ? std::atof(row[3]) : 0;

        r.total += cnt;
        lat_sum += lat * cnt; lat_cnt += cnt;

        if (result == "ok") {
            r.ok_count += cnt;
            if (sid == 1) r.s1_ok += cnt; else r.s2_ok += cnt;
        } else {
            r.ng_count += cnt;
            if (sid == 1) r.s1_ng += cnt; else r.s2_ng += cnt;
        }
    }
    if (lat_cnt > 0) r.avg_latency = lat_sum / lat_cnt;
    r.ng_rate = r.total > 0 ? (100.0 * r.ng_count / r.total) : 0.0;
    mysql_free_result(res);
    return r;
}

std::vector<StatsDao::InspectionRecord> StatsDao::get_history(
    int station_filter, const std::string& date_from,
    const std::string& date_to, int limit) {

    std::vector<InspectionRecord> records;
    PooledConnection conn(pool_);
    if (!conn.get()) return records;

    if (limit <= 0 || limit > 500) limit = 100;

    std::ostringstream sql;
    sql << "SELECT id, station_id, timestamp, result, confidence, "
        << "defect_type, image_path, latency_ms FROM inspections WHERE 1=1";
    if (station_filter > 0) sql << " AND station_id=" << station_filter;
    if (!date_from.empty()) sql << " AND timestamp>='" << date_from << "'";
    if (!date_to.empty()) sql << " AND timestamp<='" << date_to << " 23:59:59'";
    sql << " ORDER BY id DESC LIMIT " << limit;

    if (mysql_query(conn, sql.str().c_str()) != 0) return records;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return records;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        InspectionRecord r;
        r.id = row[0] ? std::atoi(row[0]) : 0;
        r.station_id = row[1] ? std::atoi(row[1]) : 0;
        r.timestamp = row[2] ? row[2] : "";
        r.result = row[3] ? row[3] : "";
        r.confidence = row[4] ? std::atof(row[4]) : 0;
        r.defect_type = row[5] ? row[5] : "";
        r.image_path = row[6] ? row[6] : "";
        r.latency_ms = row[7] ? std::atoi(row[7]) : 0;
        records.push_back(r);
    }
    mysql_free_result(res);
    return records;
}

} // namespace factory
