#include "bootstrap/composition_root.hpp"

#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bootstrap = warehouse::bootstrap;

namespace {

class MapSource final : public bootstrap::ConfigurationSource {
public:
    std::map<std::string, std::string> values{
        {"WAREHOUSE_DB_HOST", "127.0.0.1"},
        {"WAREHOUSE_DB_PORT", "3306"},
        {"WAREHOUSE_DB_NAME", "warehouse"},
        {"WAREHOUSE_DB_USER", "warehouse_app"},
        {"WAREHOUSE_DB_PASSWORD", "secret"},
    };

    std::optional<std::string> read(std::string_view key) const override {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::nullopt
                                     : std::optional<std::string>(found->second);
    }
};

class FakeStartupPlatform final : public bootstrap::StartupPlatform {
public:
    std::vector<bootstrap::StartupStage> calls;
    std::optional<bootstrap::StartupStage> failAt;
    bool stopped = false;

    bootstrap::StartupResult initializeMySqlRuntime(
        const bootstrap::ServerConfiguration&) override {
        return called(bootstrap::StartupStage::MySqlRuntime);
    }

    bootstrap::StartupResult createDatabasePool(
        const bootstrap::ServerConfiguration&) override {
        return called(bootstrap::StartupStage::DatabasePool);
    }

    bootstrap::StartupResult runMigrations() override {
        return called(bootstrap::StartupStage::Migration);
    }

    bootstrap::StartupResult runLegacyStartupChecks(
        const bootstrap::ServerConfiguration&) override {
        return called(bootstrap::StartupStage::LegacyStartupCheck);
    }

    bootstrap::StartupResult assembleApplication() override {
        return called(bootstrap::StartupStage::ApplicationAssembly);
    }

    bootstrap::StartupResult assembleApi() override {
        return called(bootstrap::StartupStage::ApiAssembly);
    }

    bootstrap::StartupResult startHttpListener(
        const bootstrap::ServerConfiguration&) override {
        return called(bootstrap::StartupStage::HttpListen);
    }

    void stop() noexcept override { stopped = true; }

private:
    bootstrap::StartupResult called(bootstrap::StartupStage stage) {
        calls.push_back(stage);
        if (failAt && *failAt == stage) {
            return bootstrap::StartupResult{bootstrap::StartupFailure{
                stage, "forced_failure", "safe test failure"}};
        }
        return {};
    }
};

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    const std::vector<bootstrap::StartupStage> expected{
        bootstrap::StartupStage::MySqlRuntime,
        bootstrap::StartupStage::DatabasePool,
        bootstrap::StartupStage::Migration,
        bootstrap::StartupStage::LegacyStartupCheck,
        bootstrap::StartupStage::ApplicationAssembly,
        bootstrap::StartupStage::ApiAssembly,
        bootstrap::StartupStage::HttpListen,
    };

    {
        MapSource source;
        FakeStartupPlatform platform;
        bootstrap::CompositionRoot root(source, platform);
        const auto result = root.start();
        expect(result.ok(), "valid startup reaches the listener");
        expect(platform.calls == expected, "startup uses the frozen order");
        root.stop();
        expect(platform.stopped, "composition root delegates orderly stop");
    }

    {
        MapSource source;
        FakeStartupPlatform platform;
        platform.failAt = bootstrap::StartupStage::Migration;
        bootstrap::CompositionRoot root(source, platform);
        const auto result = root.start();
        expect(!result.ok(), "migration failure rejects startup");
        expect(result.failure && result.failure->stage == bootstrap::StartupStage::Migration,
               "migration failure keeps its stage");
        expect(platform.calls == std::vector<bootstrap::StartupStage>({
                                     bootstrap::StartupStage::MySqlRuntime,
                                     bootstrap::StartupStage::DatabasePool,
                                     bootstrap::StartupStage::Migration,
                                 }),
               "migration failure prevents assembly and listening");
        expect(platform.stopped, "failed startup cleans up initialized resources");
    }

    {
        MapSource source;
        source.values.erase("WAREHOUSE_DB_PASSWORD");
        FakeStartupPlatform platform;
        bootstrap::CompositionRoot root(source, platform);
        const auto result = root.start();
        expect(!result.ok(), "configuration failure rejects startup");
        expect(result.failure && result.failure->stage == bootstrap::StartupStage::Configuration,
               "configuration failure keeps its stage");
        expect(platform.calls.empty(), "invalid configuration touches no platform resource");
        expect(result.failure && result.failure->message.find("secret") == std::string::npos,
               "configuration startup failure contains no secret value");
    }

    return failures == 0 ? 0 : 1;
}
