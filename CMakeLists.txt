cmake_minimum_required(VERSION 3.16)
project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

cmake_minimum_required(VERSION 3.16)

project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(warehouse_server
    src/main.cpp
)

if (WIN32)

    # Windows
    set(MYSQL_DIR "D:/Program Files/MySQL/MySQL Server 8.0")

    target_include_directories(warehouse_server PRIVATE
        "${MYSQL_DIR}/include"
    )

    target_link_libraries(warehouse_server PRIVATE
        Threads::Threads
        "${MYSQL_DIR}/lib/libmysql.lib"
        ws2_32
    )

else()

    # Linux（GitHub Actions）
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MYSQL REQUIRED mysqlclient)

    target_include_directories(warehouse_server PRIVATE
        ${MYSQL_INCLUDE_DIRS}
    )

    target_link_libraries(warehouse_server PRIVATE
        Threads::Threads
        ${MYSQL_LIBRARIES}
    )

endif()
