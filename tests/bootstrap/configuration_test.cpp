#include "bootstrap/configuration.hpp"

#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace bootstrap = warehouse::bootstrap;

namespace {

class MapSource final : public bootstrap::ConfigurationSource {
public:
    std::map<std::string, std::string> values;
    std::optional<std::string> read(std::string_view key) const override {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::nullopt : std::optional<std::string>(found->second);
    }
};

MapSource validSource() {
    MapSource source;
    source.values = {{"WAREHOUSE_DB_HOST", "127.0.0.1"},
                     {"WAREHOUSE_DB_PORT", "3306"},
                     {"WAREHOUSE_DB_NAME", "warehouse"},
                     {"WAREHOUSE_DB_USER", "warehouse_app"},
                     {"WAREHOUSE_DB_PASSWORD", "sensitive-value"}};
    return source;
}

int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    auto source = validSource();
    auto result = bootstrap::loadServerConfiguration(source);
    expect(result.ok(), "minimal legacy environment is accepted");
    expect(result.configuration->http.bindAddress == "127.0.0.1", "legacy bind default");
    expect(result.configuration->http.port == 8081, "legacy port default");
    expect(result.configuration->http.workerThreads == 1, "conservative worker default");
    expect(result.configuration->mysql.poolSize == 1, "conservative pool default");

    MapSource missing;
    auto missingResult = bootstrap::loadServerConfiguration(missing);
    expect(!missingResult.ok() && missingResult.issues.size() == 5,
           "all missing database settings are reported together");

    source = validSource();
    source.values["WAREHOUSE_DB_PORT"] = "3306x";
    source.values["WAREHOUSE_HTTP_PORT"] = "65536";
    source.values["WAREHOUSE_HTTP_WORKER_THREADS"] = "0";
    source.values["WAREHOUSE_DB_POOL_SIZE"] = "-1";
    auto invalid = bootstrap::loadServerConfiguration(source);
    expect(!invalid.ok() && invalid.issues.size() == 4, "invalid integers are rejected");

    source = validSource();
    source.values["WAREHOUSE_CORS_ALLOWED_ORIGINS"] =
        "https://warehouse.example, https://manager.example";
    auto cors = bootstrap::loadServerConfiguration(source);
    expect(cors.ok() && cors.configuration->http.allowedOrigins.size() == 2,
           "explicit CORS origins are parsed");

    source.values["WAREHOUSE_CORS_ALLOWED_ORIGINS"] = "*";
    auto wildcard = bootstrap::loadServerConfiguration(source);
    expect(!wildcard.ok(), "wildcard CORS is rejected");

    source = validSource();
    source.values["WAREHOUSE_ADMIN_USERNAME"] = "admin";
    auto incompleteAdmin = bootstrap::loadServerConfiguration(source);
    expect(!incompleteAdmin.ok(), "partial initial administrator credentials are rejected");

    source = validSource();
    source.values["WAREHOUSE_MIGRATE_PASSWORDS"] = "true";
    auto migrationFlag = bootstrap::loadServerConfiguration(source);
    expect(migrationFlag.ok() &&
               migrationFlag.configuration->legacy.migratePlaintextPasswords,
           "explicit password migration flag is parsed");

    for (const auto& issue : invalid.issues) {
        expect(issue.message.find("sensitive-value") == std::string::npos,
               "configuration errors do not disclose passwords");
    }
    return failures == 0 ? 0 : 1;
}
