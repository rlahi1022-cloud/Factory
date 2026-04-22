// ============================================================================
// inspection_service.h — 검사 결과 처리 서비스
// ============================================================================
// 목적:
//   검증 → DB 저장 → 이미지 저장을 하나의 트랜잭션으로 묶는다.
//   실패 시 이전 단계를 되돌린다.
//
// 호출자: StationHandler
// 사용 DAO: InspectionDao, AssemblyDao
// ============================================================================
#pragma once

#include "storage/dao.h"
#include "core/event_types.h"

#include <string>
#include <vector>

namespace factory {

// 처리 결과 — 호출자가 ACK/GUI 이벤트 발행에 사용
struct InspectionResult {
    bool        success = false;
    long long   inspection_id = -1;
    std::string image_path;        // 원본 JPEG 저장 경로
    std::string heatmap_path;      // Anomaly Map PNG 저장 경로 (v0.9.0+)
    std::string pred_mask_path;    // Pred Mask PNG 저장 경로 (v0.9.0+)
    std::string error_message;
};

class InspectionService {
public:
    InspectionService(ConnectionPool& pool, const std::string& image_root_dir);

    // 검증 + DB INSERT + 이미지 저장을 한 번에 수행
    InspectionResult process(const InspectionEvent& ev);

private:
    bool validate(const InspectionEvent& ev, std::string& out_error);

    // 이미지 저장 공통 헬퍼 — 임의의 바이너리를 지정 확장자로 저장한다.
    //   suffix: "original", "heatmap", "mask" 등 파일명 구분자
    //   ext:    ".jpg", ".png" 등 확장자
    std::string save_blob(int station_id,
                          const std::string& timestamp,
                          const std::vector<uint8_t>& bytes,
                          const std::string& suffix,
                          const std::string& ext);

    InspectionDao inspection_dao_;
    AssemblyDao   assembly_dao_;
    std::string   image_root_dir_;
};

} // namespace factory
