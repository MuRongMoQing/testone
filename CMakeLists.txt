cmake_minimum_required(VERSION 3.16)
project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

set(MYSQL_DIR "D:/Program Files/MySQL/MySQL Server 8.0")

add_executable(warehouse_server
    src/main.cpp
)

target_include_directories(warehouse_server PRIVATE "${MYSQL_DIR}/include")
target_link_libraries(warehouse_server PRIVATE Threads::Threads "${MYSQL_DIR}/lib/libmysql.lib")

if (WIN32)
    target_link_libraries(warehouse_server PRIVATE ws2_32)
endif()
