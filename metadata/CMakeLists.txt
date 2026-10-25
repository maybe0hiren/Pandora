cmake_minimum_required(VERSION 3.16)
project(PandoraMetadata VERSION 1.0.0 LANGUAGES CXX)

# This module builds on its own: `cmake -S metadata -B build && cmake --build build`.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(PANDORA_METADATA_BUILD_TESTS "Build the metadata unit tests" ON)
option(PANDORA_METADATA_BUILD_EXAMPLES "Build the metadata example" ON)

# Qt 6 is the target; Qt 5.15 also works (used for quick local checks).
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql)

# Shared contract (header-only). Standalone build pulls it from ../shared; the integrated build already has it.
if(NOT TARGET pandora::shared)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../shared ${CMAKE_CURRENT_BINARY_DIR}/shared)
endif()

# Embed sql/schema.sql so the documented schema is the one that actually runs.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql PANDORA_SCHEMA_SQL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SchemaEmbedded.cpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/SchemaEmbedded.cpp @ONLY)

add_library(pandora_metadata STATIC
    include/pandora/metadata/MetadataService.h
    src/MetadataService.cpp
    src/MetadataStore.h
    src/MetadataStore.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/SchemaEmbedded.cpp)
add_library(pandora::metadata ALIAS pandora_metadata)

target_include_directories(pandora_metadata
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pandora_metadata
    PUBLIC  pandora::shared Qt${QT_VERSION_MAJOR}::Core
    PRIVATE Qt${QT_VERSION_MAJOR}::Sql)

if(PANDORA_METADATA_BUILD_EXAMPLES)
    add_executable(metadata_example examples/example_usage.cpp)
    target_link_libraries(metadata_example PRIVATE pandora::metadata)
endif()

if(PANDORA_METADATA_BUILD_TESTS)
    enable_testing()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(metadata_tests tests/test_metadata.cpp)
    target_link_libraries(metadata_tests PRIVATE pandora::metadata Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME metadata_tests COMMAND metadata_tests)
endif()
