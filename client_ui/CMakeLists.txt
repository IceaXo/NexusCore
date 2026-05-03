cmake_minimum_required(VERSION 3.10)
project(nexus_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES *.cpp)

add_executable(nexus_client ${SOURCES})

# TODO: 链接 raylib
# find_package(raylib REQUIRED)
# target_link_libraries(nexus_client PRIVATE raylib)
