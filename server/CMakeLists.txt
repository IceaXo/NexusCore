cmake_minimum_required(VERSION 3.10)
project(nexus_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES
    network/*.cpp
    game/*.cpp
    ipc/*.cpp
)

add_executable(nexus_server main.cpp ${SOURCES})
target_include_directories(nexus_server PRIVATE network game ipc)
