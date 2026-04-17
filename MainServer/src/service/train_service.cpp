// ============================================================================
// train_service.cpp — 학습 완료 처리 서비스 구현
// ============================================================================
// 실패 시 롤백 전략:
//   1. validate 실패 → 바로 반환
//   2. 파일 저장 실패 → 바로 반환 (DB INSERT 안 함)
//   3. DB INSERT 실패 → 저장한 파일 삭제
// ============================================================================
#include "service/train_service.h"
#include "core/logger.h"

#include <filesystem>
#include <fstream>

namespace factory {

TrainService::TrainService(ConnectionPool& pool)
    : model_dao_(pool) {
}

TrainResult TrainService::process(const TrainCompleteEvent& ev) {
    TrainResult result;

    // 1. 검증
    if (!validate(ev, result.error_message)) {
        log_err_train("검증 실패 | %s", result.error_message.c_str());
        return result;
    }

    // 2. 모델 파일 저장
    if (!ev.model_bytes.empty()) {
        result.saved_model_path = save_model_file(ev);
        if (result.saved_model_path.empty()) {
            result.error_message = "model_file_save_failed";
            return result;
        }
    }

    // 3. DB INSERT (저장된 로컬 경로 사용)
    TrainCompleteEvent ev_copy = ev;
    if (!result.saved_model_path.empty()) {
        ev_copy.model_path = result.saved_model_path;
    }

    if (!model_dao_.insert(ev_copy)) {
        // DB 실패 → 파일 롤백 (삭제)
        if (!result.saved_model_path.empty()) {
            std::filesystem::remove(result.saved_model_path);
            log_train("DB 실패 → 모델 파일 롤백 삭제 | %s", result.saved_model_path.c_str());
        }
        result.error_message = "db_insert_failed";
        result.saved_model_path.clear();
        return result;
    }

    result.success = true;
    log_train("학습 처리 완료 | 모델=%s 버전=%s 경로=%s",
              ev.model_type.c_str(), ev.version.c_str(), result.saved_model_path.c_str());
    return result;
}

bool TrainService::validate(const TrainCompleteEvent& ev, std::string& out_error) {
    if (ev.request_id.empty()) {
        out_error = "empty_request_id";
        return false;
    }
    if (ev.station_id < 1 || ev.station_id > 2) {
        out_error = "invalid_station_id";
        return false;
    }
    if (ev.version.empty()) {
        out_error = "empty_version";
        return false;
    }
    return true;
}

std::string TrainService::save_model_file(const TrainCompleteEvent& ev) {
    // 원본 확장자 추출
    std::string ext = ".bin";
    auto dot_pos = ev.model_path.rfind('.');
    if (dot_pos != std::string::npos) {
        ext = ev.model_path.substr(dot_pos);
    }

    std::string dir = "./storage/models/station" + std::to_string(ev.station_id);
    std::filesystem::create_directories(dir);

    std::string save_path = dir + "/" + ev.version + ext;
    std::ofstream ofs(save_path, std::ios::binary);
    if (!ofs) {
        log_err_train("모델 파일 저장 실패 | %s", save_path.c_str());
        return "";
    }

    ofs.write(reinterpret_cast<const char*>(ev.model_bytes.data()),
              static_cast<std::streamsize>(ev.model_bytes.size()));
    log_train("모델 파일 저장 완료 | %s (%zu bytes)", save_path.c_str(), ev.model_bytes.size());
    return save_path;
}

} // namespace factory
