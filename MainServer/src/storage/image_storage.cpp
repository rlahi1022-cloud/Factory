// ============================================================================
// image_storage.cpp — NG 이미지 파일 저장 구현
// ============================================================================
// IMAGE_SAVE_REQUESTED 이벤트를 수신하면 InspectionEvent의 image_bytes를
// 로컬 파일시스템에 JPEG로 저장한다.
//
// 저장 경로: {root_dir}/station{N}/{YYYYMMDD}/ng_{epoch_ms}.jpg
// 디렉터리가 없으면 자동 생성한다 (std::filesystem::create_directories).
// ============================================================================
#include "storage/image_storage.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include "core/logger.h"
#include <sstream>

namespace fs = std::filesystem;

namespace factory {

ImageStorage::ImageStorage(EventBus& bus, const std::string& root_dir)
    : event_bus_(bus),
      root_dir_(root_dir) {
}

void ImageStorage::register_handlers() {
    event_bus_.subscribe(EventType::IMAGE_SAVE_REQUESTED,
                         [this](const std::any& p) { this->on_image_save(p); });
}

/// 이미지 바이너리를 파일로 저장한다.
/// image_bytes가 비어 있으면(OK 판정 등) 저장을 건너뛴다.
void ImageStorage::on_image_save(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    if (ev.image_bytes.empty()) return;  // OK 판정이면 이미지 없음

    std::string save_path = make_save_path(root_dir_, ev.station_id, ev.timestamp);
    fs::create_directories(fs::path(save_path).parent_path());  // 날짜 디렉터리 자동 생성

    std::ofstream ofs(save_path, std::ios::binary);
    if (!ofs) {
        log_err_img("파일 열기 실패 | %s", save_path.c_str());
        return;
    }
    ofs.write(reinterpret_cast<const char*>(ev.image_bytes.data()),
              static_cast<std::streamsize>(ev.image_bytes.size()));
    log_img("이미지 저장 완료 | %s", save_path.c_str());
}

std::string ImageStorage::make_save_path(const std::string& root_dir,
                                         int station_id,
                                         const std::string& timestamp) {
    // ISO8601 타임스탬프("2026-04-17T10:30:00")에서 YYYYMMDD를 추출.
    // 타임스탬프가 짧거나 비정상이면 현재 로컬 시각을 대체 사용한다.
    std::string yyyymmdd;
    if (timestamp.size() >= 10) {
        yyyymmdd = timestamp.substr(0, 4) + timestamp.substr(5, 2) + timestamp.substr(8, 2);
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

    // epoch 밀리초를 파일명에 사용하여 동일 시각 충돌을 방지
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::ostringstream os;
    os << root_dir << "/station" << station_id << "/" << yyyymmdd
       << "/ng_" << ms << ".jpg";
    return os.str();
}

} // namespace factory
