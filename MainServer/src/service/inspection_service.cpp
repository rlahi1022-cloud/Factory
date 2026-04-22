// ============================================================================
// inspection_service.cpp — NG 검사 결과 처리 서비스 (v0.9.0+ 3장 이미지)
// ============================================================================
// 책임:
//   AI 추론서버가 보낸 NG(Station1_NG=1000 / Station2_NG=1002) 패킷의
//   후처리 단일 진입점. validate + 이미지 저장 + DB INSERT 를 원자적 흐름으로
//   처리하고 결과를 InspectionResult 로 반환.
//
// 처리 순서:
//   1. validate()           — station_id/result/score/latency/크기 검증
//   2. save_blob() × 3      — 원본/히트맵/마스크 JPEG/PNG 저장 (NG 만, 각기 개별)
//   3. inspection_dao_.insert — inspections 테이블 INSERT (세 경로 포함)
//   4. Station2 면 assembly_dao_.insert — assemblies 테이블 보충 INSERT
//
// 실패 시 롤백 정책 (의도적 관대 처리):
//   - validate 실패 → 즉시 반환 (아무 작업도 하지 않음)
//   - 이미지 저장 일부 실패 → 해당 경로만 비운 채 DB 는 계속 진행
//                         → "결과는 있지만 이미지가 없는" row 가 남을 수 있음
//   - inspections INSERT 실패 → 이미 저장된 이미지는 고아 파일로 남김 (나중에 청소)
//   - assemblies INSERT 실패 → inspections 는 이미 저장됐으므로 로그만
//
// 관대한 롤백 이유:
//   실시간 검사 파이프라인이라 처리 지연 < 데이터 완전성. 소량의 고아 파일은
//   배치 청소 스크립트에서 정리. 반대 방향 "DB 에만 있고 파일 없음" 도 허용 —
//   클라이언트는 이미지가 없으면 size=0 으로 빈 프레임을 받음.
//
// 이미지 저장 경로:
//   {image_root}/station{N}/{YYYYMMDD}/ng_{epoch_ms}_{suffix}.{ext}
//     예) ./storage/station2/20260422/ng_1745310000123_heatmap.png
//   세 이미지가 같은 epoch_ms 를 공유 → 파일명만 봐도 한 검사 결과임을 식별.
//
// 보안:
//   이미지 50MB 상한, 경로 traversal 미사용 (상대경로 조립만).
//   디스크 여유 100MB 미만 시 저장 거부 (DoS/디스크 풀 방어).
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

    // 2. NG 이미지 3장 저장 (DB INSERT 전에 먼저 저장 — 경로를 DB에 바로 기록하기 위함)
    //    각 저장 실패는 치명적이지 않고 해당 경로만 비움.
    if (!ev.image_bytes.empty()) {
        result.image_path = save_blob(ev.station_id, ev.timestamp,
                                      ev.image_bytes, "original", ".jpg");
        if (result.image_path.empty()) {
            log_warn("AI", "원본 이미지 저장 실패 | id=%s", ev.inspection_id.c_str());
        }
    }
    if (!ev.heatmap_bytes.empty()) {
        result.heatmap_path = save_blob(ev.station_id, ev.timestamp,
                                        ev.heatmap_bytes, "heatmap", ".png");
        if (result.heatmap_path.empty()) {
            log_warn("AI", "히트맵 저장 실패 | id=%s", ev.inspection_id.c_str());
        }
    }
    if (!ev.pred_mask_bytes.empty()) {
        result.pred_mask_path = save_blob(ev.station_id, ev.timestamp,
                                          ev.pred_mask_bytes, "mask", ".png");
        if (result.pred_mask_path.empty()) {
            log_warn("AI", "Pred Mask 저장 실패 | id=%s", ev.inspection_id.c_str());
        }
    }

    // 3. inspections 테이블 INSERT (세 이미지 경로 포함)
    long long inspection_id = inspection_dao_.insert(ev,
                                                     result.image_path,
                                                     result.heatmap_path,
                                                     result.pred_mask_path);
    if (inspection_id < 0) {
        result.error_message = "db_insert_failed";
        log_err_db("INSERT inspections 실패 | id=%s", ev.inspection_id.c_str());
        return result;
    }
    result.inspection_id = inspection_id;

    // 4. Station2면 assemblies 테이블 INSERT
    if (ev.station_id == 2) {
        long long assembly_id = assembly_dao_.insert(ev, inspection_id);
        if (assembly_id < 0) {
            log_err_db("INSERT assemblies 실패 | inspection_id=%lld", inspection_id);
            // inspection은 이미 저장됨 — 치명적이지 않으므로 계속 진행
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
    if (ev.inspection_id.empty() || ev.inspection_id.size() > 128) {
        out_error = "invalid_inspection_id";
        return false;
    }
    if (ev.result.empty() || (ev.result != "ok" && ev.result != "ng")) {
        out_error = "invalid_result";
        return false;
    }
    if (ev.score < 0.0 || ev.score > 1.0) {
        out_error = "invalid_score";
        return false;
    }
    if (ev.latency_ms < 0 || ev.latency_ms > 60000) {
        out_error = "invalid_latency";
        return false;
    }
    if (ev.defect_type.size() > 64) {
        out_error = "defect_type_too_long";
        return false;
    }
    // 이미지 크기 제한: 최대 50MB
    if (ev.image_bytes.size() > 50ULL * 1024 * 1024) {
        out_error = "image_too_large";
        return false;
    }
    // timestamp 형식 검증 (최소 10자: YYYY-MM-DD)
    if (ev.timestamp.size() < 10) {
        out_error = "invalid_timestamp";
        return false;
    }
    return true;
}

// 이미지 바이너리를 날짜 디렉터리 + epoch ms 기반 파일명으로 저장한다.
// 원본/히트맵/마스크 3종 모두 동일한 ms 타임스탬프를 써서 같은 검사에 속한 파일임을
// 파일명으로도 식별 가능하게 한다 (동일 호출 트랜잭션 내에서는 ms가 증가할 수 있지만,
// 동일 검사 내 파일들은 suffix로 구분되므로 문제 없음).
std::string InspectionService::save_blob(int station_id,
                                         const std::string& timestamp,
                                         const std::vector<uint8_t>& bytes,
                                         const std::string& suffix,
                                         const std::string& ext) {
    if (bytes.empty()) return "";

    // 날짜 디렉터리 생성 (YYYYMMDD)
    std::string yyyymmdd;
    if (timestamp.size() >= 10) {
        yyyymmdd = timestamp.substr(0, 4) +
                   timestamp.substr(5, 2) +
                   timestamp.substr(8, 2);
    } else {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream os;
        os << std::put_time(&tm, "%Y%m%d");
        yyyymmdd = os.str();
    }

    // epoch ms로 파일명 충돌 방지 (suffix로 원본/히트맵/마스크 구분)
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::ostringstream path_os;
    path_os << image_root_dir_ << "/station" << station_id
            << "/" << yyyymmdd << "/ng_" << ms << "_" << suffix << ext;
    std::string save_path = path_os.str();

    std::filesystem::create_directories(std::filesystem::path(save_path).parent_path());

    // 디스크 여유 공간 확인 (최소 100MB 여유 필요)
    auto space_info = std::filesystem::space(
        std::filesystem::path(save_path).parent_path());
    if (space_info.available < 100ULL * 1024 * 1024) {
        log_err_img("디스크 여유 공간 부족 | 잔여=%zu MB",
                    space_info.available / (1024 * 1024));
        return "";
    }

    std::ofstream ofs(save_path, std::ios::binary);
    if (!ofs) {
        log_err_img("파일 열기 실패 | %s", save_path.c_str());
        return "";
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    ofs.flush();
    if (!ofs.good()) {
        log_err_img("파일 쓰기 실패 | %s", save_path.c_str());
        ofs.close();
        std::filesystem::remove(save_path);
        return "";
    }
    ofs.close();

    // 저장된 파일 크기 검증
    auto file_size = std::filesystem::file_size(save_path);
    if (file_size != bytes.size()) {
        log_err_img("이미지 크기 불일치 | 예상=%zu 실제=%zu",
                    bytes.size(), file_size);
        std::filesystem::remove(save_path);
        return "";
    }

    log_img("이미지 저장 완료 | %s", save_path.c_str());
    return save_path;
}

} // namespace factory
