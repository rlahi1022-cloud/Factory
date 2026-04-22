// ============================================================================
// db_manager.cpp — DB 이벤트 핸들러 구현 (DAO 기반)
// ============================================================================
// DAO에 DB 작업을 위임하고, 이벤트 발행(ACK/NACK/MODEL_RELOAD)만 담당한다.
// ============================================================================
#include "storage/db_manager.h"
#include "Protocol.h"
#include "core/logger.h"

#include <filesystem>
#include <fstream>

namespace factory {

DbManager::DbManager(EventBus& bus, ConnectionPool& pool)
    : event_bus_(bus),
      pool_(pool),
      inspection_dao_(pool),
      assembly_dao_(pool),
      model_dao_(pool) {
}

void DbManager::register_handlers() {
    event_bus_.subscribe(EventType::DB_WRITE_REQUESTED,
                         [this](const std::any& p) { this->on_db_write(p); });
    // TRAIN_COMPLETE_RECEIVED는 TrainHandler(Service 레이어)에서 처리
    // 여기서 중복 구독하면 이중 DB INSERT + 이중 ACK 발생
}

// ── 검사 결과 DB 저장 ──
void DbManager::on_db_write(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    long long inspection_id = inspection_dao_.insert(ev);
    if (inspection_id < 0) {
        log_err_db("INSERT inspections 실패");
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

    // Station2는 assemblies 테이블에 추가 저장
    if (ev.station_id == static_cast<int>(StationId::ASSEMBLY)) {
        if (assembly_dao_.insert(ev, inspection_id) < 0) {
            log_err_db("INSERT assemblies 실패");
        }
    }

    event_bus_.publish(EventType::DB_WRITE_COMPLETED, ev);
}

// ── 학습 완료 → 모델 저장 + DB INSERT + 리로드 요청 ──
void DbManager::on_train_complete(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainCompleteEvent&>(payload);

    log_train("학습 완료 수신 | 모델=%s 버전=%s 정확도=%.4f 파일=%zu bytes",
              ev.model_type.c_str(), ev.version.c_str(), ev.accuracy,
              ev.model_bytes.size());

    // 모델 파일 바이너리를 로컬에 저장
    std::string saved_path;
    if (!ev.model_bytes.empty()) {
        std::string ext = ".bin";
        auto dot_pos = ev.model_path.rfind('.');
        if (dot_pos != std::string::npos) {
            ext = ev.model_path.substr(dot_pos);
        }

        std::string dir = "./storage/models/station" + std::to_string(ev.station_id);
        std::filesystem::create_directories(dir);
        saved_path = dir + "/" + ev.version + ext;

        std::ofstream ofs(saved_path, std::ios::binary);
        if (ofs) {
            ofs.write(reinterpret_cast<const char*>(ev.model_bytes.data()),
                      static_cast<std::streamsize>(ev.model_bytes.size()));
            log_train("모델 파일 저장 완료 | %s (%zu bytes)",
                      saved_path.c_str(), ev.model_bytes.size());
        } else {
            log_err_train("모델 파일 저장 실패 | %s", saved_path.c_str());
            saved_path.clear();
        }
    }

    // DB에 모델 정보 INSERT (저장된 로컬 경로 사용)
    TrainCompleteEvent ev_copy = ev;
    if (!saved_path.empty()) {
        ev_copy.model_path = saved_path;
    }

    if (model_dao_.insert(ev_copy)) {
        log_db("INSERT models 성공");

        // 학습 서버에 ACK
        AckSendEvent ack{};
        ack.protocol_no    = static_cast<int>(ProtocolNo::TRAIN_COMPLETE_ACK);
        ack.inspection_id  = ev.request_id;
        ack.sender_addr    = ev.sender_addr;
        ack.ack_ok         = true;
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, ack);

        // 추론서버에 모델 리로드 명령
        if (!ev.model_bytes.empty()) {
            ModelReloadEvent reload{};
            reload.station_id  = ev.station_id;
            reload.model_path  = saved_path.empty() ? ev.model_path : saved_path;
            reload.version     = ev.version;
            reload.model_type  = ev.model_type;
            reload.model_bytes = ev.model_bytes;
            event_bus_.publish(EventType::MODEL_RELOAD_REQUESTED, reload);
            log_train("추론서버 모델 리로드 요청 발행 | 스테이션=%d", ev.station_id);
        }
    } else {
        log_err_db("INSERT models 실패");
    }
}

} // namespace factory
