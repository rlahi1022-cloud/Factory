// ImageStorage.cpp
#include "storage/image_storage.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

void ImageStorage::on_image_save(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    if (ev.image_bytes.empty()) return;

    std::string save_path = make_save_path(root_dir_, ev.station_id, ev.timestamp);
    fs::create_directories(fs::path(save_path).parent_path());

    std::ofstream ofs(save_path, std::ios::binary);
    if (!ofs) {
        std::cerr << "[ImageStorage] open failed: " << save_path << std::endl;
        return;
    }
    ofs.write(reinterpret_cast<const char*>(ev.image_bytes.data()),
              static_cast<std::streamsize>(ev.image_bytes.size()));
    std::cout << "[ImageStorage] saved " << save_path << std::endl;
}

std::string ImageStorage::make_save_path(const std::string& root_dir,
                                         int station_id,
                                         const std::string& timestamp) {
    // YYYYMMDD 추출 (timestamp가 ISO8601 "YYYY-MM-DDTHH:..." 라고 가정)
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

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::ostringstream os;
    os << root_dir << "/station" << station_id << "/" << yyyymmdd
       << "/ng_" << ms << ".jpg";
    return os.str();
}

} // namespace factory
