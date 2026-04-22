# Factory Main Server (C++17, EventBus 아키텍처)
cmake_minimum_required(VERSION 3.16)
project(FactoryMainServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

set(SRC_FILES
    # core
    src/core/main.cpp
    src/core/event_bus.cpp
    src/core/tcp_listener.cpp
    src/core/config.cpp

    # handler
    src/handler/router.cpp
    src/handler/station_handler.cpp
    src/handler/ack_sender.cpp
    src/handler/train_handler.cpp

    # service
    src/service/inspection_service.cpp
    src/service/train_service.cpp

    # storage
    src/storage/connection_pool.cpp
    src/storage/dao.cpp
    src/storage/password_hash.cpp

    # session
    src/session/gui_notifier.cpp
    src/session/gui_tcp_listener.cpp
    src/session/gui_router.cpp
    src/session/gui_service.cpp
    src/session/session_manager.cpp

    # monitor
    src/monitor/health_checker.cpp
    src/monitor/connection_registry.cpp
)

add_executable(factory_main_server ${SRC_FILES})

if(WIN32)
    target_link_libraries(factory_main_server PRIVATE ws2_32 libmariadb)
else()
    target_link_libraries(factory_main_server PRIVATE pthread mariadb crypt)
endif()
