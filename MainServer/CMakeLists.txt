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
    src/main.cpp
    src/event_bus.cpp
    src/tcp_listener.cpp
    src/router.cpp
    src/station_handler.cpp
    src/db_manager.cpp
    src/image_storage.cpp
    src/gui_notifier.cpp
    src/health_checker.cpp
    src/ack_sender.cpp
    src/connection_registry.cpp
)

add_executable(factory_main_server ${SRC_FILES})

if(WIN32)
    target_link_libraries(factory_main_server PRIVATE ws2_32)
else()
    target_link_libraries(factory_main_server PRIVATE pthread)
endif()
