cmake_minimum_required(VERSION 3.16)
project(PandoraShared LANGUAGES CXX)

# Header-only library holding the shared contract (Node, Pandora, FileMetadata, Message, Constants, Protocol).
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

add_library(pandora_shared INTERFACE)
add_library(pandora::shared ALIAS pandora_shared)
target_include_directories(pandora_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pandora_shared INTERFACE Qt${QT_VERSION_MAJOR}::Core)
target_compile_features(pandora_shared INTERFACE cxx_std_17)
