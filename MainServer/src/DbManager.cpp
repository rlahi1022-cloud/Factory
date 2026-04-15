// DbManager.cpp
#include "DbManager.h"
#include "Protocol.h"

#include <iostream>

namespace factory {

DbManager::DbManager(EventBus& bus,
                     const std::string& host,
                     const std::string& user,
                     const std::string& password,
                     const std::string& schema)
    : event_bus_(bus),
      db_host_(host),
      db_user_(user),
      db_password_(password),
      db_schema_(schema) {
}

void DbManager::register_handlers() {
    event_bus_.subscribe(EventType::DB_WRITE_REQUESTED,
                         [this](const std::any& p) { this->on_db_write(p); });
}

bool DbManager::connect() {
    // TODO: mysql_real_connect / mariadb 커넥터 사용
    std::cout << "[DbManager] connect to " << db_host_ << "/" << db_schema_ << std::endl;
    return true;
}

void DbManager::disconnect() {
    // TODO
}

void DbManager::on_db_write(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    long long inspection_id = 0;
    if (!insert_inspection(ev, inspection_id)) {
        std::cerr << "[DbManager] insert_inspection failed" << std::endl;
        return;
    }
    if (ev.station_id == static_cast<int>(StationId::ASSEMBLY)) {
        insert_assembly(ev, inspection_id);
    }
}

bool DbManager::insert_inspection(const InspectionEvent& ev, long long& out_inspection_id) {
    // TODO: prepared statement 사용
    // INSERT INTO inspections (station_id, bottle_id, model_id, timestamp, result,
    //   confidence, defect_type, image_path, latency_ms) VALUES (...)
    out_inspection_id = 1; // placeholder
    std::cout << "[DbManager] INSERT inspections station=" << ev.station_id
              << " result=" << ev.result << std::endl;
    return true;
}

bool DbManager::insert_assembly(const InspectionEvent& ev, long long inspection_id) {
    // TODO: INSERT INTO assemblies (inspection_id, bottle_id, cap_ok, label_ok,
    //   fill_ok, yolo_detections, patchcore_score, timestamp) VALUES (...)
    std::cout << "[DbManager] INSERT assemblies inspection_id=" << inspection_id << std::endl;
    return true;
}

} // namespace factory
