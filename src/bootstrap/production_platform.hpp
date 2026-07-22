#pragma once

#include "bootstrap/composition_root.hpp"

#include <memory>

namespace warehouse::bootstrap {

class ProductionPlatform final : public StartupPlatform {
public:
    ProductionPlatform();
    ~ProductionPlatform() override;

    ProductionPlatform(const ProductionPlatform&) = delete;
    ProductionPlatform& operator=(const ProductionPlatform&) = delete;

    StartupResult initializeMySqlRuntime(
        const ServerConfiguration& configuration) override;
    StartupResult createDatabasePool(
        const ServerConfiguration& configuration) override;
    StartupResult runMigrations() override;
    StartupResult runLegacyStartupChecks(
        const ServerConfiguration& configuration) override;
    StartupResult assembleApplication() override;
    StartupResult assembleApi() override;
    StartupResult startHttpListener(
        const ServerConfiguration& configuration) override;
    bool listenerHealthy() const noexcept;
    void stop() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace warehouse::bootstrap
