cmake_minimum_required(VERSION 3.21)
project(Pandora VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Sql Concurrent)

qt_standard_project_setup()

add_executable(Pandora
    src/main.cpp
    src/core/types.cpp
    src/core/event_bus.cpp
    src/security/session_security.cpp
    src/network/peer_node.cpp
    src/metadata/metadata_store.cpp
    src/storage/storage_manager.cpp
    src/core/pandora_controller.cpp
    src/ui/main_window.cpp
    include/core/types.h
    include/core/event_bus.h
    include/security/session_security.h
    include/network/peer_node.h
    include/metadata/metadata_store.h
    include/storage/storage_manager.h
    include/core/pandora_controller.h
    include/ui/main_window.h
)

target_include_directories(Pandora PRIVATE include)
target_link_libraries(Pandora PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Concurrent)

if(MSVC)
    target_compile_options(Pandora PRIVATE /W4 /permissive-)
else()
    target_compile_options(Pandora PRIVATE -Wall -Wextra -Wpedantic)
endif()
