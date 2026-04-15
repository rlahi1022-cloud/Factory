#pragma once
// ImageStorage.h
// IMAGE_SAVE_REQUESTED 이벤트 구독 → ./storage/stationN/YYYYMMDD/*.jpg 저장

#include "EventBus.h"

#include <string>

namespace factory {

class ImageStorage {
public:
    ImageStorage(EventBus& bus, const std::string& root_dir);
    void register_handlers();

private:
    void on_image_save(const std::any& payload);

    static std::string make_save_path(const std::string& root_dir,
                                      int station_id,
                                      const std::string& timestamp);

    EventBus&   event_bus_;
    std::string root_dir_;
};

} // namespace factory
