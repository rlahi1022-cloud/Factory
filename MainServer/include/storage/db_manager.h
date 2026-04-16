#pragma once
// db_manager.h
// MariaDB 연동. DB_WRITE_REQUESTED 이벤트를 구독하여
// inspections / assemblies 테이블에 INSERT.
// mariadb/mysql.h (MariaDB Connector/C) 사용.

#include "core/event_bus.h"

#include <mariadb/mysql.h>
#include <mutex>
#include <string>

namespace factory {

class DbManager {
public:
    DbManager(EventBus& bus,
              const std::string& host,
              const std::string& user,
              const std::string& password,
              const std::string& schema,
              unsigned int port = 3306);
    ~DbManager();

    void register_handlers();

    bool connect();
    void disconnect();

private:
    void on_db_write(const std::any& payload);

    bool insert_inspection(const InspectionEvent& ev, long long& out_inspection_id);
    bool insert_assembly(const InspectionEvent& ev, long long inspection_id);

    // raw_json에서 int 필드 추출 (간이 파서)
    static int extract_int(const std::string& json, const std::string& key);
    static double extract_double(const std::string& json, const std::string& key);
    static std::string extract_str(const std::string& json, const std::string& key);

    EventBus&    event_bus_;
    MYSQL*       conn_;
    std::mutex   db_mutex_;     // DB 접근 직렬화
    std::string  db_host_;
    std::string  db_user_;
    std::string  db_password_;
    std::string  db_schema_;
    unsigned int db_port_;
};

} // namespace factory
