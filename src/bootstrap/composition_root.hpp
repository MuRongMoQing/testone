#pragma once

#include "bootstrap/configuration.hpp"
#include "bootstrap/lifecycle.hpp"

namespace warehouse::bootstrap {

class StartupPlatform {
public:
    virtual ~StartupPlatform() = default;

    virtual StartupResult initializeMySqlRuntime(
        const ServerConfiguration& configuration) = 0;
    virtual StartupResult createDatabasePool(
        const ServerConfiguration& configuration) = 0;
    virtual StartupResult runMigrations() = 0;
    virtual StartupResult runLegacyStartupChecks(
        const ServerConfiguration& configuration) = 0;
    virtual StartupResult assembleApplication() = 0;
    virtual StartupResult assembleApi() = 0;
    virtual StartupResult startHttpListener(
        const ServerConfiguration& configuration) = 0;
    virtual void stop() noexcept = 0;
};

class CompositionRoot {
public:
    CompositionRoot(const ConfigurationSource& configurationSource,
                    StartupPlatform& platform) noexcept;
    ~CompositionRoot();

    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;

    StartupResult start();
    StartupResult run(ShutdownCoordinator& shutdown);
    void stop() noexcept;

private:
    const ConfigurationSource& configurationSource_;
    StartupPlatform& platform_;
    bool platformTouched_ = false;
    bool started_ = false;
    bool stopped_ = false;
};

}  // namespace warehouse::bootstrap
