cmake_minimum_required(VERSION 3.16)

project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(warehouse_server
    src/main.cpp
)

target_link_libraries(warehouse_server PRIVATE Threads::Threads)

# ===============================
# Windows
# ===============================
if(WIN32)

    # 如果使用 vcpkg
    find_package(unofficial-libmysql REQUIRED)

    target_link_libraries(warehouse_server PRIVATE
        unofficial::libmysql::libmysql
        ws2_32
    )

# ===============================
# Linux
# ===============================
else()

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(MYSQL REQUIRED mysqlclient)

    target_include_directories(warehouse_server PRIVATE
        ${MYSQL_INCLUDE_DIRS}
    )

    target_link_directories(warehouse_server PRIVATE
        ${MYSQL_LIBRARY_DIRS}
    )

    target_link_libraries(warehouse_server PRIVATE
        ${MYSQL_LIBRARIES}
    )

endif()
