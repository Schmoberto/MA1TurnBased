cmake_minimum_required(VERSION 4.1)
project(MA1TurnBased)

set(CMAKE_CXX_STANDARD 23)

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(MA1TurnBased main.cpp)
target_link_libraries(MA1TurnBased PRIVATE SDL3::SDL3 imgui::imgui)
