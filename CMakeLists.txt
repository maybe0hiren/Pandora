cmake_minimum_required(VERSION 3.21)
project(Pandora VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Sql Concurrent Test)

qt_standard_project_setup()

add_library(PandoraCore STATIC
    src/core/types.cpp
    src/core/event_bus.cpp
    src/security/session_security.cpp
    src/network/peer_node.cpp
    src/metadata/metadata_store.cpp
    src/storage/storage_manager.cpp
    src/core/pandora_controller.cpp
    include/core/types.h
    include/core/event_bus.h
    include/security/session_security.h
    include/network/peer_node.h
    include/metadata/metadata_store.h
    include/storage/storage_manager.h
    include/core/pandora_controller.h
)

target_include_directories(PandoraCore PUBLIC include)
target_link_libraries(PandoraCore PUBLIC Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Concurrent)

add_executable(Pandora
    src/main.cpp
    src/ui/main_window.cpp
    include/ui/main_window.h
)

target_link_libraries(Pandora PRIVATE PandoraCore)

enable_testing()

add_executable(PandoraTests
    tests/pandora_distributed_tests.cpp
)

target_link_libraries(PandoraTests PRIVATE PandoraCore Qt6::Test)
add_test(NAME PandoraDistributedTests COMMAND PandoraTests)

if(MSVC)
    target_compile_options(PandoraCore PRIVATE /W4 /permissive-)
    target_compile_options(Pandora PRIVATE /W4 /permissive-)
    target_compile_options(PandoraTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PandoraCore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(Pandora PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(PandoraTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
