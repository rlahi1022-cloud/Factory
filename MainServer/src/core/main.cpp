// main.cpp
// Factory 메인 운영 서버 진입점
// - EventBus를 중심으로 컴포넌트들을 구독자로 등록
// - TcpListener, GuiTcpListener, HealthChecker가 별도 스레드를 보유

#include "core/event_bus.h"
#include "core/tcp_listener.h"
#include "handler/router.h"
#include "handler/station_handler.h"
#include "handler/ack_sender.h"
#include "storage/db_manager.h"
#include "storage/image_storage.h"
#include "session/gui_notifier.h"
#include "session/gui_tcp_listener.h"
#include "monitor/health_checker.h"
#include "Protocol.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
constexpr uint16_t GUI_LISTEN_PORT = 9010;

// DB 접속 정보 (공통)
const char* DB_HOST     = "127.0.0.1";
const char* DB_USER     = "factorymanager";
const char* DB_PASSWORD = "1234";
const char* DB_SCHEMA   = "Factory";

std::atomic<bool> g_should_exit{false};
void on_signal(int) { g_should_exit.store(true); }
} // namespace

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    using namespace factory;

    // 1) EventBus 생성 및 시작
    EventBus event_bus;
    event_bus.start();

    // 2) 컴포넌트 생성 + 핸들러 등록
    Router router(event_bus);
    router.register_handlers();

    Station1Handler station1_handler(event_bus);
    station1_handler.register_handlers();

    Station2Handler station2_handler(event_bus);
    station2_handler.register_handlers();

    DbManager db_manager(event_bus, DB_HOST, DB_USER, DB_PASSWORD, DB_SCHEMA);
    db_manager.connect();
    db_manager.register_handlers();

    ImageStorage image_storage(event_bus, "./storage");
    image_storage.register_handlers();

    GuiNotifier gui_notifier(event_bus);
    gui_notifier.register_handlers();

    AckSender ack_sender(event_bus);
    ack_sender.register_handlers();

    // 3) TCP 리스너 시작 (추론 서버로부터 패킷 수신)
    TcpListener tcp_listener(event_bus, MAIN_SERVER_PORT);
    tcp_listener.start();

    // 4) GUI TCP 리스너 시작 (MFC 클라이언트 접속 + DB 조회)
    GuiTcpListener gui_tcp_listener(event_bus, GUI_LISTEN_PORT,
                                    DB_HOST, DB_USER, DB_PASSWORD, DB_SCHEMA);
    gui_tcp_listener.start();

    // 5) 헬스체크 시작
    std::vector<HealthTarget> targets = {
        {"ai_inference_1", "127.0.0.1", 9101},
        {"ai_inference_2", "127.0.0.1", 9102},
        {"ai_training",    "127.0.0.1", 9201},
    };
    HealthChecker health_checker(event_bus, targets);
    health_checker.start();

    std::cout << "[Main] Factory main server running. Ctrl+C to exit." << std::endl;
    while (!g_should_exit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "[Main] shutting down..." << std::endl;
    health_checker.stop();
    gui_tcp_listener.stop();
    tcp_listener.stop();
    db_manager.disconnect();
    event_bus.stop();
    return 0;
}
