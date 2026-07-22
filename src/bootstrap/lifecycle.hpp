#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace warehouse::bootstrap {

enum class StartupStage {
    Configuration,
    MySqlRuntime,
    DatabasePool,
    Migration,
    LegacyStartupCheck,
    ApplicationAssembly,
    ApiAssembly,
    HttpListen,
    BackgroundWorkers,
};

struct StartupFailure {
    StartupStage stage = StartupStage::Configuration;
    std::string code;
    std::string message;
};

struct StartupResult {
    std::optional<StartupFailure> failure;
    bool ok() const noexcept;
};

class ShutdownCoordinator {
public:
    void requestStop() noexcept;
    bool stopRequested() const noexcept;
    void wait();
    bool waitFor(std::chrono::milliseconds timeout);

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool stopRequested_ = false;
};

}  // namespace warehouse::bootstrap
