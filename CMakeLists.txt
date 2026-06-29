cmake_minimum_required(VERSION 3.16)
project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(warehouse_server
    src/main.cpp
)

target_link_libraries(warehouse_server PRIVATE Threads::Threads)

if (WIN32)
    target_link_libraries(warehouse_server PRIVATE ws2_32)
endif()
