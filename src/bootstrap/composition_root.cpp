#include "bootstrap/composition_root.hpp"

#include <string>

namespace warehouse::bootstrap {
namespace {

StartupResult configurationFailure(const ConfigurationResult& result) {
    std::string message = "invalid server configuration";
    for (const auto& issue : result.issues) {
        message += "; ";
        message += issue.key;
        message += ": ";
        message += issue.message;
    }
    return StartupResult{StartupFailure{
        StartupStage::Configuration, "invalid_configuration", std::move(message)}};
}

StartupResult alreadyStartedFailure() {
    return StartupResult{StartupFailure{
        StartupStage::Configuration, "already_started",
        "the composition root can only be started once"}};
}

}  // namespace

CompositionRoot::CompositionRoot(const ConfigurationSource& configurationSource,
                                 StartupPlatform& platform) noexcept
    : configurationSource_(configurationSource), platform_(platform) {}

CompositionRoot::~CompositionRoot() { stop(); }

StartupResult CompositionRoot::start() {
    if (started_ || stopped_ || platformTouched_) return alreadyStartedFailure();

    auto loaded = loadServerConfiguration(configurationSource_);
    if (!loaded.ok()) return configurationFailure(loaded);
    const auto& configuration = *loaded.configuration;

    platformTouched_ = true;
    StartupResult result = platform_.initializeMySqlRuntime(configuration);
    if (result.ok()) result = platform_.createDatabasePool(configuration);
    if (result.ok()) result = platform_.runMigrations();
    if (result.ok()) result = platform_.runLegacyStartupChecks(configuration);
    if (result.ok()) result = platform_.assembleApplication();
    if (result.ok()) result = platform_.assembleApi();
    if (result.ok()) result = platform_.startHttpListener(configuration);

    if (!result.ok()) {
        stop();
        return result;
    }

    started_ = true;
    return {};
}

StartupResult CompositionRoot::run(ShutdownCoordinator& shutdown) {
    auto result = start();
    if (!result.ok()) return result;
    shutdown.wait();
    stop();
    return {};
}

void CompositionRoot::stop() noexcept {
    if (stopped_ || !platformTouched_) return;
    stopped_ = true;
    started_ = false;
    platform_.stop();
}

}  // namespace warehouse::bootstrap
