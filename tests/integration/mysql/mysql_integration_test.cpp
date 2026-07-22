#include "infrastructure/mysql/client_library.hpp"
#include "infrastructure/mysql/connection.hpp"
#include "infrastructure/mysql/connection_pool.hpp"
#include "infrastructure/mysql/migration.hpp"
#include "infrastructure/mysql/transaction.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mysql = warehouse::infrastructure::mysql;

namespace {

std::optional<std::string> environment(const char* name) {
    const char* value = std::getenv(name);
    if (!value || *value == '\0') return std::nullopt;
    return std::string(value);
}

bool scalarEquals(const mysql::SqlResult& result, const std::string& expected) {
    return result && result.value().rows.size() == 1 &&
           result.value().rows.front().size() == 1 &&
           result.value().rows.front().front() &&
           *result.value().rows.front().front() == expected;
}

void requireSql(mysql::SqlExecutor& executor, std::string_view sql) {
    auto result = executor.executeTrusted(sql);
    if (!result) {
        std::cerr << "integration SQL failed: " << result.error().message << '\n';
        std::abort();
    }
}

void clean(mysql::SqlExecutor& executor) {
    requireSql(executor, "DROP TABLE IF EXISTS stage1_binary_probe");
    requireSql(executor, "DROP TABLE IF EXISTS stage1_isolation_probe");
    requireSql(executor, "DROP TABLE IF EXISTS schema_migration_steps");
    requireSql(executor, "DROP TABLE IF EXISTS schema_migrations");
    requireSql(executor, "DROP TABLE IF EXISTS goods");
    requireSql(executor, "DROP TABLE IF EXISTS users");
}

}  // namespace

int main() {
    const auto host = environment("WAREHOUSE_TEST_DB_HOST");
    const auto database = environment("WAREHOUSE_TEST_DB_NAME");
    const auto username = environment("WAREHOUSE_TEST_DB_USER");
    const auto password = environment("WAREHOUSE_TEST_DB_PASSWORD");
    const auto allowChanges = environment("WAREHOUSE_TEST_DB_ALLOW_SCHEMA_CHANGES");
    if (!host || !database || !username || !password ||
        !allowChanges || *allowChanges != "YES" ||
        database->find("test") == std::string::npos) {
        std::cout << "SKIP: set dedicated WAREHOUSE_TEST_DB_* variables, use a database "
                     "name containing 'test', and set WAREHOUSE_TEST_DB_ALLOW_SCHEMA_CHANGES=YES\n";
        return 77;
    }

    mysql::MySqlConnectionOptions options;
    options.host = *host;
    options.database = *database;
    options.username = *username;
    options.password = *password;
    if (const auto port = environment("WAREHOUSE_TEST_DB_PORT")) {
        options.port = static_cast<unsigned int>(std::stoul(*port));
    }

    auto library = mysql::ClientLibrary::create();
    assert(library);
    {
        auto connection = mysql::MySqlConnection::connect(options);
        assert(connection);
        auto& executor = *connection.value();
        clean(executor);

        auto definitions = mysql::discoverMigrations("migrations");
        auto bootstrap = mysql::readMigrationBootstrapSql("migrations");
        assert(definitions && bootstrap);
        for (const auto& step : definitions.value().front().steps) {
            requireSql(executor, step.applySql);
        }
        auto seededUser = executor.executePrepared(
            "INSERT INTO users(username,password,role) VALUES(?,?,'viewer')",
            {std::string("legacy-user"), std::string("legacy-password")});
        assert(seededUser && seededUser.value().affectedRows == 1);
        auto seededGoods = executor.executePrepared(
            "INSERT INTO goods(name,location,status,stored_at,operator) "
            "VALUES(?,?,'stored',UTC_TIMESTAMP(),?)",
            {std::string("legacy-sentinel"), std::string("legacy-location"),
             std::string("integration-test")});
        assert(seededGoods && seededGoods.value().affectedRows == 1);

        mysql::MigrationRunner runner(executor);
        const auto first = runner.run(definitions.value(), bootstrap.value(), 2);
        assert(first.status == mysql::MigrationRunStatus::Succeeded);
        assert(first.versionsApplied == 1);
        assert(first.stepsApplied == 2);
        assert(first.pendingSteps.empty());
        assert(first.nonRollbackableSteps.size() == 2);
        assert(scalarEquals(executor.executePrepared(
                                "SELECT COUNT(*) FROM users WHERE username=?",
                                {std::string("legacy-user")}),
                            "1"));

        requireSql(executor,
                   "UPDATE schema_migrations SET status='FAILED',finished_at=NULL "
                   "WHERE version=1");
        requireSql(executor,
                   "UPDATE schema_migration_steps SET status='RUNNING',finished_at=NULL "
                   "WHERE version=1 AND step_id='002_goods'");
        const auto recovered = runner.run(definitions.value(), bootstrap.value(), 2);
        assert(recovered.status == mysql::MigrationRunStatus::Succeeded);
        assert(recovered.versionsApplied == 1);
        assert(recovered.stepsApplied == 1);
        assert(recovered.alreadyCompleteSteps.size() == 1);
        assert(recovered.pendingSteps.empty());

        const auto repeated = runner.run(definitions.value(), bootstrap.value(), 2);
        assert(repeated.status == mysql::MigrationRunStatus::Succeeded);
        assert(repeated.versionsAlreadyComplete == 1);
        assert(repeated.stepsApplied == 0);
        assert(repeated.alreadyCompleteSteps.size() == 2);
        assert(repeated.pendingSteps.empty());
        assert(scalarEquals(executor.executePrepared(
                                "SELECT COUNT(*) FROM goods WHERE name=? AND location=?",
                                {std::string("legacy-sentinel"),
                                 std::string("legacy-location")}),
                            "1"));

        requireSql(executor,
                   "CREATE TABLE stage1_isolation_probe (id INT PRIMARY KEY) ENGINE=InnoDB");
        auto peerConnection = mysql::MySqlConnection::connect(options);
        assert(peerConnection);
        auto& peer = *peerConnection.value();

        auto command = mysql::Transaction::begin(
            executor, warehouse::application::WorkUnitMode::Command);
        assert(command);
        assert(scalarEquals(executor.executeTrusted(
                                "SELECT COUNT(*) FROM stage1_isolation_probe"),
                            "0"));
        requireSql(peer, "INSERT INTO stage1_isolation_probe(id) VALUES(1)");
        assert(scalarEquals(executor.executeTrusted(
                                "SELECT COUNT(*) FROM stage1_isolation_probe"),
                            "1"));
        command.value().rollback();

        requireSql(executor, "DELETE FROM stage1_isolation_probe");

        auto snapshot = mysql::Transaction::begin(
            executor, warehouse::application::WorkUnitMode::FinalizedRead);
        assert(snapshot);
        assert(scalarEquals(executor.executeTrusted(
                                "SELECT COUNT(*) FROM stage1_isolation_probe"),
                            "0"));
        requireSql(peer, "INSERT INTO stage1_isolation_probe(id) VALUES(2)");
        assert(scalarEquals(executor.executeTrusted(
                                "SELECT COUNT(*) FROM stage1_isolation_probe"),
                            "0"));
        snapshot.value().rollback();

        requireSql(executor,
                   "CREATE TABLE stage1_binary_probe (id INT PRIMARY KEY, payload LONGBLOB NOT NULL) "
                   "ENGINE=InnoDB");
        std::vector<unsigned char> payload(128 * 1024);
        for (std::size_t index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<unsigned char>(index % 251);
        }
        auto binary = executor.executePrepared(
            "INSERT INTO stage1_binary_probe(id,payload) VALUES(?,?)",
            {std::int64_t{1}, payload});
        assert(binary && binary.value().affectedRows == 1);
        assert(scalarEquals(executor.executePrepared(
                                "SELECT OCTET_LENGTH(payload) FROM stage1_binary_probe WHERE id=?",
                                {std::int64_t{1}}),
                            std::to_string(payload.size())));

        clean(executor);
    }

    std::size_t created = 0;
    {
        mysql::ConnectionPool pool(1, [&] {
            ++created;
            auto connection = mysql::MySqlConnection::connect(options);
            if (!connection) {
                return warehouse::application::Result<std::unique_ptr<mysql::SqlExecutor>,
                                                      mysql::SqlError>::failure(
                    connection.error());
            }
            std::unique_ptr<mysql::SqlExecutor> executor = std::move(connection.value());
            return warehouse::application::Result<std::unique_ptr<mysql::SqlExecutor>,
                                                  mysql::SqlError>::success(
                std::move(executor));
        });
        {
            auto first = pool.acquire(std::chrono::seconds(2));
            assert(first);
            first.value().executor().poison();
        }
        auto replacement = pool.acquire(std::chrono::seconds(2));
        assert(replacement);
        assert(created == 2);
    }
    mysql::releaseThreadContext();
}
