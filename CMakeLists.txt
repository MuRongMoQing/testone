cmake_minimum_required(VERSION 3.16)

project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Threads REQUIRED)

if(WIN32)
    find_package(unofficial-libmysql CONFIG REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
    get_target_property(MYSQL_INCLUDE_ROOT
        unofficial::libmysql::libmysql INTERFACE_INCLUDE_DIRECTORIES)
    set(MYSQL_INCLUDE_DIR "${MYSQL_INCLUDE_ROOT}/mysql")

    set(MYSQL_TARGET unofficial::libmysql::libmysql)
    set(SODIUM_TARGET unofficial-sodium::sodium)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MYSQL REQUIRED IMPORTED_TARGET mysqlclient)
    pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
    set(MYSQL_TARGET PkgConfig::MYSQL)
    set(SODIUM_TARGET PkgConfig::SODIUM)
endif()

add_library(warehouse_password_hash src/password_hash.cpp)
target_include_directories(warehouse_password_hash PUBLIC src)
target_link_libraries(warehouse_password_hash PUBLIC ${SODIUM_TARGET})

add_executable(warehouse_server src/main.cpp)
target_link_libraries(warehouse_server PRIVATE
    Threads::Threads
    ${MYSQL_TARGET}
    warehouse_password_hash
)
if(WIN32)
    target_include_directories(warehouse_server PRIVATE "${MYSQL_INCLUDE_DIR}")
endif()

if(WIN32)
    target_link_libraries(warehouse_server PRIVATE ws2_32)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(password_hash_test tests/password_hash_test.cpp)
    target_link_libraries(password_hash_test PRIVATE warehouse_password_hash)
    add_test(NAME password_hash COMMAND password_hash_test)
endif()
