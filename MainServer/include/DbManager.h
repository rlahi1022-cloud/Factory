#pragma once
// DbManager.h
// MariaDB 연동 (골격). DB_WRITE_REQUESTED 이벤트를 구독하여 inspections / assemblies 테이블에 INSERT.
// 실제 연결은 mysqlclient 또는 mariadb-connector-cpp 사용 권장.

#include "EventBus.h"

#include <string>

namespace factory {

class DbManager {
public:
    DbManager(EventBus& bus,
              const std::string& host,
              const std::string& user,
              const std::string& password,
              const std::string& schema);

    void register_handlers();

    // 실제 연결/해제는 connect()/disconnect()에서 (TODO)
    bool connect();
    void disconnect();

private:
    void on_db_write(const std::any& payload);

    // 실제 INSERT 수행 (TODO 구현)
    bool insert_inspection(const InspectionEvent& ev, long long& out_inspection_id);
    bool insert_assembly(const InspectionEvent& ev, long long inspection_id);

    EventBus&   event_bus_;
    std::string db_host_;
    std::string db_user_;
    std::string db_password_;
    std::string db_schema_;
};

} // namespace factory
