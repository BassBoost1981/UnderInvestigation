cmake_minimum_required(VERSION 3.24)
project(UISim LANGUAGES CXX)

# UISim: engine-independent simulation core of "Under Investigation".
# Rule: standard C++20 only, no Unreal types (see docs/DESIGN.md, chapter 4.2).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UISIM_BUILD_TESTS "Build the UISim unit tests" ON)

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(nlohmann_json)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(uisim STATIC
    src/Track.cpp
    src/DataLoader.cpp
    src/Sim.cpp
    src/Rating.cpp
    src/Season.cpp
    src/Career.cpp
    src/Hearing.cpp)
target_include_directories(uisim PUBLIC include)
target_link_libraries(uisim PRIVATE nlohmann_json::nlohmann_json)

# Absolute path of the repository's data folder, baked into a generated header
# so the runner and tests find tracks/entries regardless of the working directory.
get_filename_component(UISIM_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data" ABSOLUTE)
configure_file(cmake/DataDir.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/uisim/DataDir.h" @ONLY)
add_library(uisim_datadir INTERFACE)
target_include_directories(uisim_datadir INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_executable(uisim_runner tools/runner/main.cpp)
target_link_libraries(uisim_runner PRIVATE uisim uisim_datadir)

if(UISIM_BUILD_TESTS)
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz)
    FetchContent_MakeAvailable(doctest)

    add_executable(uisim_tests
        tests/test_main.cpp
        tests/test_track.cpp
        tests/test_sim.cpp
        tests/test_race_control.cpp
        tests/test_replay.cpp
        tests/test_penalties.cpp
        tests/test_accidents.cpp
        tests/test_tyres.cpp
        tests/test_pitlane.cpp
        tests/test_track_limits.cpp
        tests/test_tracks.cpp
        tests/test_lapped_cars.cpp
        tests/test_pit_rules.cpp
        tests/test_weather.cpp
        tests/test_wet_tyres.cpp
        tests/test_rain_control.cpp
        tests/test_standing_start.cpp
        tests/test_season.cpp
        tests/test_career.cpp
        tests/test_hearing.cpp)
    target_link_libraries(uisim_tests PRIVATE uisim uisim_datadir doctest::doctest)

    enable_testing()
    add_test(NAME uisim_tests COMMAND uisim_tests)
endif()
