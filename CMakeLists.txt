cmake_minimum_required(VERSION 3.16)

project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL 3 REQUIRED)

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

add_library(warehouse_domain INTERFACE)
target_include_directories(warehouse_domain INTERFACE src)

add_library(warehouse_application
    src/application/legacy/legacy_warehouse_service.cpp
)
target_include_directories(warehouse_application PUBLIC src)
target_link_libraries(warehouse_application PUBLIC warehouse_domain)

add_library(warehouse_password_hash src/password_hash.cpp)
target_include_directories(warehouse_password_hash PUBLIC src)
target_link_libraries(warehouse_password_hash PUBLIC ${SODIUM_TARGET})

add_library(warehouse_security
    src/infrastructure/security/sodium_password_hasher.cpp
)
target_include_directories(warehouse_security PUBLIC src)
target_link_libraries(warehouse_security PUBLIC warehouse_application warehouse_password_hash)

add_library(warehouse_session
    src/infrastructure/session/in_memory_legacy_session_store.cpp
)
target_include_directories(warehouse_session PUBLIC src)
target_link_libraries(warehouse_session PUBLIC warehouse_application ${SODIUM_TARGET})

add_library(warehouse_mysql
    src/infrastructure/mysql/client_library.cpp
    src/infrastructure/mysql/connection.cpp
    src/infrastructure/mysql/connection_pool.cpp
    src/infrastructure/mysql/legacy_unit_of_work.cpp
    src/infrastructure/mysql/migration.cpp
    src/infrastructure/mysql/sha256.cpp
    src/infrastructure/mysql/transaction.cpp
)
target_include_directories(warehouse_mysql PUBLIC src)
target_link_libraries(warehouse_mysql PUBLIC warehouse_application PRIVATE ${MYSQL_TARGET})
if(WIN32)
    target_include_directories(warehouse_mysql PRIVATE "${MYSQL_INCLUDE_DIR}")
endif()

add_library(warehouse_api src/api/legacy_http_server.cpp)
target_include_directories(warehouse_api PUBLIC src)
target_link_libraries(warehouse_api
    PUBLIC warehouse_application
    PRIVATE httplib::httplib nlohmann_json::nlohmann_json Threads::Threads
)

add_library(warehouse_bootstrap_core
    src/bootstrap/composition_root.cpp
    src/bootstrap/configuration.cpp
    src/bootstrap/lifecycle.cpp
    src/bootstrap/signal_shutdown.cpp
)
target_include_directories(warehouse_bootstrap_core PUBLIC src)
target_link_libraries(warehouse_bootstrap_core PUBLIC Threads::Threads)

add_library(warehouse_bootstrap
    src/bootstrap/legacy_startup.cpp
    src/bootstrap/production_platform.cpp
    src/bootstrap/run_server.cpp
)
target_include_directories(warehouse_bootstrap PUBLIC src)
target_link_libraries(warehouse_bootstrap
    PUBLIC warehouse_bootstrap_core
    PRIVATE warehouse_api warehouse_mysql warehouse_security warehouse_session
            warehouse_password_hash OpenSSL::Crypto Threads::Threads
)

add_executable(warehouse_server src/main.cpp)
target_link_libraries(warehouse_server PRIVATE warehouse_bootstrap)

include(CTest)
if(BUILD_TESTING)
    function(add_warehouse_test name source target)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE src tests)
        target_link_libraries(${name} PRIVATE ${target})
        if(MSVC)
            target_compile_options(${name} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${name} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_warehouse_test(password_hash_test tests/password_hash_test.cpp warehouse_password_hash)
    add_warehouse_test(legacy_warehouse_service_test
        tests/application/legacy_warehouse_service_test.cpp warehouse_application)
    add_warehouse_test(configuration_test tests/bootstrap/configuration_test.cpp
        warehouse_bootstrap_core)
    add_warehouse_test(lifecycle_test tests/bootstrap/lifecycle_test.cpp
        warehouse_bootstrap_core)
    add_warehouse_test(composition_root_test tests/bootstrap/composition_root_test.cpp
        warehouse_bootstrap_core)
    add_warehouse_test(signal_shutdown_test tests/bootstrap/signal_shutdown_test.cpp
        warehouse_bootstrap_core)
    add_warehouse_test(legacy_startup_test tests/bootstrap/legacy_startup_test.cpp
        warehouse_bootstrap)
    add_warehouse_test(connection_pool_test
        tests/unit/infrastructure/mysql/connection_pool_test.cpp warehouse_mysql)
    add_warehouse_test(transaction_test
        tests/unit/infrastructure/mysql/transaction_test.cpp warehouse_mysql)
    add_warehouse_test(legacy_unit_of_work_test
        tests/unit/infrastructure/mysql/legacy_unit_of_work_test.cpp warehouse_mysql)
    add_warehouse_test(migration_test
        tests/unit/infrastructure/mysql/migration_test.cpp warehouse_mysql)
    add_warehouse_test(migration_runner_test
        tests/unit/infrastructure/mysql/migration_runner_test.cpp warehouse_mysql)
    add_warehouse_test(mysql_integration_test
        tests/integration/mysql/mysql_integration_test.cpp warehouse_mysql)
    set_tests_properties(mysql_integration_test PROPERTIES SKIP_RETURN_CODE 77)
    add_warehouse_test(legacy_http_server_test
        tests/api/legacy_http_server_test.cpp warehouse_api)
    target_link_libraries(legacy_http_server_test PRIVATE
        httplib::httplib nlohmann_json::nlohmann_json Threads::Threads)
    set_tests_properties(migration_test mysql_integration_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
