// ============================================================================
// inspection_service.cpp — 검사 결과 처리 서비스 구현
// ============================================================================
// 실패 시 롤백 전략:
//   1. validate 실패 → 바로 반환 (아무것도 안 함)
//   2. DB INSERT 실패 → 이미지 안 저장, NACK
//   3. Assembly INSERT 실패 → inspection은 이미 저장됨 (로그만)
//   4. 이미지 저장 실패 → DB는 성공, image_path만 비워둠 (로그만)
// ============================================================================
#include "service/inspection_service.h"
#include "core/logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace factory {

InspectionService::InspectionService(ConnectionPool& pool, const std::string& image_root_dir)
    : inspection_dao_(pool),
      assembly_dao_(pool),
      image_root_dir_(image_root_dir) {
}

InspectionResult InspectionService::process(const InspectionEvent& ev) {
    InspectionResult result;

    // 1. 검증
    if (!validate(ev, result.error_message)) {
        log_err_ai("검증 실패 | %s", result.error_message.c_str());
        return result;
    }

    // 2. inspections 테이블 INSERT
    long long inspection_id = inspection_dao_.insert(ev);
    if (inspection_id < 0) {
        result.error_message = "db_insert_failed";
        log_err_db("INSERT inspections 실패 | id=%s", ev.inspection_id.c_str());
        return result;
    }
    result.inspection_id = inspection_id;

    // 3. Station2면 assemblies 테이블 INSERT
    if (ev.station_id == 2) {
        long long assembly_id = assembly_dao_.insert(ev, inspection_id);
        if (assembly_id < 0) {
            log_err_db("INSERT assemblies 실패 | inspection_id=%lld", inspection_id);
            // inspection은 이미 저장됨 — 치명적이지 않으므로 계속 진행
        }
    }

    // 4. NG 이미지 저장
    if (!ev.image_bytes.empty()) {
        result.image_path = save_image(ev);
        if (result.image_path.empty()) {
            log_warn("AI", "이미지 저장 실패 — DB 기록은 유지 | id=%s", ev.inspection_id.c_str());
        }
    }

    result.success = true;
    return result;
}

bool InspectionService::validate(const InspectionEvent& ev, std::string& out_error) {
    if (ev.station_id < 1 || ev.station_id > 2) {
        out_error = "invalid_station_id";
        return false;
    }
    if (ev.inspection_id.empty()) {
        out_error = "empty_inspection_id";
        return false;
    }
    if (ev.result.empty()) {
        out_error = "empty_result";
        return false;
    }
    if (ev.score < 0.0 || ev.score > 1.0) {
        out_error = "invalid_score";
        return false;
    }
    return true;
}

std::string InspectionService::save_image(const InspectionEvent& ev) {
    // 날짜 디렉터리 생성 (YYYYMMDD)
    std::string yyyymmdd;
    if (ev.timestamp.size() >= 10) {
        yyyymmdd = ev.timestamp.substr(0, 4) +
                   ev.timestamp.substr(5, 2) +
                   ev.timestamp.substr(8, 2);
    } else {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
        localtime_r(&now, &tm);
        std::ostringstream os;
        os << std::put_time(&tm, "%Y%m%d");
        yyyymmdd = os.str();
    }

    // epoch ms로 파일명 충돌 방지
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::ostringstream path_os;
    path_os << image_root_dir_ << "/station" << ev.station_id
            << "/" << yyyymmdd << "/ng_" << ms << ".jpg";
    std::string save_path = path_os.str();

    std::filesystem::create_directories(std::filesystem::path(save_path).parent_path());

    std::ofstream ofs(save_path, std::ios::binary);
    if (!ofs) {
        log_err_img("파일 열기 실패 | %s", save_path.c_str());
        return "";
    }
    ofs.write(reinterpret_cast<const char*>(ev.image_bytes.data()),
              static_cast<std::streamsize>(ev.image_bytes.size()));
    log_img("이미지 저장 완료 | %s", save_path.c_str());
    return save_path;
}

} // namespace factory
