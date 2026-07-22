#include "bootstrap/legacy_startup.hpp"

#include "infrastructure/mysql/connection_pool.hpp"
#include "password_hash.hpp"

#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace bootstrap = warehouse::bootstrap;
namespace mysql = warehouse::infrastructure::mysql;

class StartupExecutor final : public mysql::SqlExecutor {
public:
    explicit StartupExecutor(std::string storedPassword)
        : storedPassword(std::move(storedPassword)) {}

    mysql::SqlResult executeTrusted(std::string_view) override {
        return mysql::SqlResult::success({});
    }
    mysql::SqlResult executePrepared(
        std::string_view sql, const std::vector<mysql::SqlValue>&) override {
        if (sql.find("SELECT id,password") == 0) {
            return mysql::SqlResult::success(
                {{{std::string("1"), storedPassword}}, 0, 0});
        }
        if (sql.find("SELECT COUNT(*)") == 0) {
            return mysql::SqlResult::success({{{std::string("1")}}, 0, 0});
        }
        return mysql::SqlResult::success({});
    }
    bool reusable() const noexcept override { return !poisoned; }
    void poison() noexcept override { poisoned = true; }

    std::string storedPassword;
    bool poisoned = false;
};

mysql::ConnectionPool makePool(std::string password) {
    return mysql::ConnectionPool(1, [password = std::move(password)]() mutable {
        std::unique_ptr<mysql::SqlExecutor> executor =
            std::make_unique<StartupExecutor>(std::move(password));
        return warehouse::application::Result<std::unique_ptr<mysql::SqlExecutor>,
                                              mysql::SqlError>::success(std::move(executor));
    });
}

int main() {
    std::string error;
    assert(warehouse::security::initializePasswordHashing(error));
    std::string hash;
    assert(warehouse::security::hashPassword("strong-password", hash, error));

    auto validPool = makePool(hash);
    bootstrap::LegacyStartupConfiguration valid;
    auto accepted = bootstrap::runLegacyStartupChecks(validPool, valid);
    assert(accepted.ok());

    auto plaintextPool = makePool("legacy-plaintext");
    bootstrap::LegacyStartupConfiguration disabled;
    auto rejected = bootstrap::runLegacyStartupChecks(plaintextPool, disabled);
    assert(!rejected.ok());
    assert(rejected.failure->code == "plaintext_passwords_present");
    warehouse::security::clearSensitiveString(hash);
}
