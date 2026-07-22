#pragma once

#include "bootstrap/lifecycle.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

namespace warehouse::bootstrap {

class SignalShutdownBridge {
public:
    explicit SignalShutdownBridge(
        ShutdownCoordinator& shutdown,
        std::chrono::milliseconds pollInterval = std::chrono::milliseconds(25));
    ~SignalShutdownBridge();

    SignalShutdownBridge(const SignalShutdownBridge&) = delete;
    SignalShutdownBridge& operator=(const SignalShutdownBridge&) = delete;

    bool installed() const noexcept;

private:
    using SignalHandler = void (*)(int);

    ShutdownCoordinator& shutdown_;
    std::chrono::milliseconds pollInterval_;
    SignalHandler previousInterrupt_ = SIG_DFL;
    SignalHandler previousTerminate_ = SIG_DFL;
    std::atomic<bool> finished_{false};
    std::thread watcher_;
    bool installed_ = false;
};

}  // namespace warehouse::bootstrap
