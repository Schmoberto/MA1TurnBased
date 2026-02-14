cmake_minimum_required(VERSION 4.1)
project(MA1TurnBased)

set(CMAKE_CXX_STANDARD 23)

set(SOURCES
    src/main.cpp
    src/Board.cpp
    src/Board.h
    src/Tile.cpp
    src/Tile.h
    src/Game.cpp
    src/Game.h
    src/NetworkManager.cpp
    src/NetworkManager.h
)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)

add_executable(MA1TurnBased)

target_compile_definitions(MA1TurnBased PRIVATE
    JSON_USE_IMPLICIT_CONVERSIONS=0
)

target_sources(MA1TurnBased PRIVATE ${SOURCES})

target_link_libraries(MA1TurnBased PRIVATE
        SDL3::SDL3
        imgui::imgui
        protobuf::libprotobuf
        GameNetworkingSockets::shared
        concurrentqueue::concurrentqueue
        nlohmann_json::nlohmann_json)

target_include_directories(MA1TurnBased PRIVATE
    src
    ${concurrentqueue_SOURCE_DIR}
)
