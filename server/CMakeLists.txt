cmake_minimum_required(VERSION 3.10)
project(nexus_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES
    network/*.cpp
    game/*.cpp
    ipc/*.cpp
)

# 生产可执行文件（排除测试文件）
list(FILTER SOURCES EXCLUDE REGEX "test_.*\.cpp$")

add_executable(nexus_server main.cpp ${SOURCES})
target_include_directories(nexus_server PRIVATE network game ipc)
target_link_options(nexus_server PRIVATE -static-libstdc++ -static-libgcc -static)

# 测试可执行文件（每个测试自带 main()）
foreach(TEST_FILE test_cardrule test_types test_comprehensive)
    add_executable(${TEST_FILE} game/${TEST_FILE}.cpp ${SOURCES})
    target_include_directories(${TEST_FILE} PRIVATE network game ipc)
endforeach()
