#include "bootstrap/production_platform.hpp"

#include "api/legacy_http_server.hpp"
#include "application/legacy/legacy_warehouse_service.hpp"
#include "bootstrap/legacy_startup.hpp"
#include "infrastructure/mysql/client_library.hpp"
#include "infrastructure/mysql/connection.hpp"
#include "infrastructure/mysql/connection_pool.hpp"
#include "infrastructure/mysql/legacy_unit_of_work.hpp"
#include "infrastructure/mysql/migration.hpp"
#include "infrastructure/security/sodium_password_hasher.hpp"
#include "infrastructure/session/in_memory_legacy_session_store.hpp"
#include "password_hash.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace warehouse::bootstrap {
namespace {

StartupResult failure(StartupStage stage, std::string code, std::string message) {
    return StartupResult{StartupFailure{
        stage, std::move(code), std::move(message)}};
}

}  // namespace

class ProductionPlatform::Impl {
public:
    HttpServerConfiguration http;
    std::unique_ptr<infrastructure::mysql::ClientLibrary> clientLibrary;
    std::unique_ptr<infrastructure::mysql::ConnectionPool> pool;
    std::unique_ptr<infrastructure::mysql::MySqlLegacyUnitOfWorkFactory> units;
    std::unique_ptr<infrastructure::session::InMemoryLegacySessionStore> sessions;
    std::unique_ptr<infrastructure::security::SodiumPasswordHasher> passwords;
    std::unique_ptr<application::legacy::LegacyWarehouseService> application;
    std::unique_ptr<api::LegacyHttpServer> server;
    std::thread listener;
};

ProductionPlatform::ProductionPlatform() : impl_(std::make_unique<Impl>()) {}
ProductionPlatform::~ProductionPlatform() { stop(); }

StartupResult ProductionPlatform::initializeMySqlRuntime(
    const ServerConfiguration& configuration) {
    std::string hashingError;
    if (!security::initializePasswordHashing(hashingError)) {
        return failure(StartupStage::MySqlRuntime, "libsodium_initialization_failed",
                       std::move(hashingError));
    }
    auto library = infrastructure::mysql::ClientLibrary::create();
    if (!library) {
        return failure(StartupStage::MySqlRuntime, "mysql_runtime_initialization_failed",
                       library.error().message);
    }
    impl_->http = configuration.http;
    impl_->clientLibrary = std::move(library.value());
    return {};
}

StartupResult ProductionPlatform::createDatabasePool(
    const ServerConfiguration& configuration) {
    infrastructure::mysql::MySqlConnectionOptions options;
    options.host = configuration.mysql.host;
    options.port = configuration.mysql.port;
    options.database = configuration.mysql.database;
    options.username = configuration.mysql.username;
    options.password = configuration.mysql.password;
    try {
        impl_->pool = std::make_unique<infrastructure::mysql::ConnectionPool>(
            configuration.mysql.poolSize, [options] {
                auto connection = infrastructure::mysql::MySqlConnection::connect(options);
                if (!connection) {
                    return application::Result<
                        std::unique_ptr<infrastructure::mysql::SqlExecutor>,
                        infrastructure::mysql::SqlError>::failure(connection.error());
                }
                std::unique_ptr<infrastructure::mysql::SqlExecutor> executor =
                    std::move(connection.value());
                return application::Result<
                    std::unique_ptr<infrastructure::mysql::SqlExecutor>,
                    infrastructure::mysql::SqlError>::success(std::move(executor));
            });
    } catch (const std::exception& error) {
        return failure(StartupStage::DatabasePool, "database_pool_creation_failed", error.what());
    }
    auto probe = impl_->pool->acquire(std::chrono::seconds(10));
    if (!probe) {
        return failure(StartupStage::DatabasePool, "database_connection_failed",
                       probe.error().message);
    }
    return {};
}

StartupResult ProductionPlatform::runMigrations() {
    auto definitions = infrastructure::mysql::discoverMigrations(
        std::filesystem::path("migrations"));
    if (!definitions) {
        return failure(StartupStage::Migration, "migration_discovery_failed",
                       definitions.error().message);
    }
    auto bootstrapSql = infrastructure::mysql::readMigrationBootstrapSql(
        std::filesystem::path("migrations"));
    if (!bootstrapSql) {
        return failure(StartupStage::Migration, "migration_bootstrap_discovery_failed",
                       bootstrapSql.error().message);
    }
    auto lease = impl_->pool->acquire(std::chrono::seconds(10));
    if (!lease) {
        return failure(StartupStage::Migration, "migration_connection_failed",
                       lease.error().message);
    }
    infrastructure::mysql::MigrationRunner runner(lease.value().executor());
    auto report = runner.run(definitions.value(), bootstrapSql.value());
    if (report.status != infrastructure::mysql::MigrationRunStatus::Succeeded) {
        return failure(StartupStage::Migration, "migration_failed",
                       report.failedStep.empty()
                           ? report.message
                           : report.failedStep + ": " + report.message);
    }
    return {};
}

StartupResult ProductionPlatform::runLegacyStartupChecks(
    const ServerConfiguration& configuration) {
    return bootstrap::runLegacyStartupChecks(*impl_->pool, configuration.legacy);
}

StartupResult ProductionPlatform::assembleApplication() {
    try {
        impl_->units =
            std::make_unique<infrastructure::mysql::MySqlLegacyUnitOfWorkFactory>(*impl_->pool);
        impl_->sessions =
            std::make_unique<infrastructure::session::InMemoryLegacySessionStore>();
        impl_->passwords =
            std::make_unique<infrastructure::security::SodiumPasswordHasher>();
        impl_->application = std::make_unique<application::legacy::LegacyWarehouseService>(
            *impl_->units, *impl_->sessions, *impl_->passwords);
        return {};
    } catch (const std::exception& error) {
        return failure(StartupStage::ApplicationAssembly, "application_assembly_failed",
                       error.what());
    }
}

StartupResult ProductionPlatform::assembleApi() {
    try {
        api::HttpServerOptions options;
        options.allowedOrigins = impl_->http.allowedOrigins;
        options.workerThreads = impl_->http.workerThreads;
        impl_->server =
            std::make_unique<api::LegacyHttpServer>(*impl_->application, std::move(options));
        return {};
    } catch (const std::exception& error) {
        return failure(StartupStage::ApiAssembly, "api_assembly_failed", error.what());
    }
}

StartupResult ProductionPlatform::startHttpListener(
    const ServerConfiguration& configuration) {
    if (!impl_->server->bind(configuration.http.bindAddress, configuration.http.port)) {
        return failure(StartupStage::HttpListen, "http_bind_failed",
                       "HTTP listener could not bind to the configured address");
    }
    std::promise<bool> completion;
    auto completed = completion.get_future();
    impl_->listener = std::thread([this, completion = std::move(completion)]() mutable {
        try {
            completion.set_value(impl_->server->listenAfterBind());
        } catch (...) {
            completion.set_value(false);
        }
    });
    constexpr auto readinessTimeout = std::chrono::seconds(1);
    constexpr auto pollInterval = std::chrono::milliseconds(5);
    const auto deadline = std::chrono::steady_clock::now() + readinessTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (completed.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
            const bool listenerResult = completed.get();
            if (impl_->listener.joinable()) impl_->listener.join();
            return failure(StartupStage::HttpListen, "http_listen_failed",
                           listenerResult
                               ? "HTTP listener stopped before startup completed"
                               : "HTTP listener failed before startup completed");
        }
        if (impl_->server->isRunning()) return {};
        std::this_thread::sleep_for(pollInterval);
    }
    impl_->server->stop();
    if (impl_->listener.joinable()) impl_->listener.join();
    return failure(StartupStage::HttpListen, "http_listen_timeout",
                   "HTTP listener did not become ready before the startup deadline");
}

bool ProductionPlatform::listenerHealthy() const noexcept {
    return impl_ && impl_->server && impl_->server->isRunning();
}

void ProductionPlatform::stop() noexcept {
    if (!impl_) return;
    if (impl_->server) impl_->server->stop();
    if (impl_->listener.joinable()) impl_->listener.join();
    impl_->server.reset();
    impl_->application.reset();
    impl_->passwords.reset();
    impl_->sessions.reset();
    impl_->units.reset();
    impl_->pool.reset();
    infrastructure::mysql::releaseThreadContext();
    impl_->clientLibrary.reset();
}

}  // namespace warehouse::bootstrap
