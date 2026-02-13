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
)

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)

add_executable(MA1TurnBased)

target_sources(MA1TurnBased PRIVATE ${SOURCES})

target_link_libraries(MA1TurnBased PRIVATE
        SDL3::SDL3
        imgui::imgui
        GameNetworkingSockets::shared
        concurrentqueue::concurrentqueue
        nlohmann_json::nlohmann_json)
