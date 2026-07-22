#include "bootstrap/signal_shutdown.hpp"

#include <chrono>
#include <thread>

namespace warehouse::bootstrap {
namespace {

volatile std::sig_atomic_t processSignalPending = 0;

extern "C" void recordProcessSignal(int) { processSignalPending = 1; }

}  // namespace

SignalShutdownBridge::SignalShutdownBridge(ShutdownCoordinator& shutdown,
                                           std::chrono::milliseconds pollInterval)
    : shutdown_(shutdown),
      pollInterval_(pollInterval.count() > 0 ? pollInterval
                                              : std::chrono::milliseconds(1)) {
    processSignalPending = 0;
    previousInterrupt_ = std::signal(SIGINT, recordProcessSignal);
    if (previousInterrupt_ == SIG_ERR) return;

    previousTerminate_ = std::signal(SIGTERM, recordProcessSignal);
    if (previousTerminate_ == SIG_ERR) {
        std::signal(SIGINT, previousInterrupt_);
        return;
    }

    installed_ = true;
    watcher_ = std::thread([this] {
        while (!finished_.load(std::memory_order_acquire)) {
            if (processSignalPending != 0) {
                shutdown_.requestStop();
                return;
            }
            std::this_thread::sleep_for(pollInterval_);
        }
    });
}

SignalShutdownBridge::~SignalShutdownBridge() {
    finished_.store(true, std::memory_order_release);
    if (watcher_.joinable()) watcher_.join();
    if (installed_) {
        std::signal(SIGTERM, previousTerminate_);
        std::signal(SIGINT, previousInterrupt_);
    }
}

bool SignalShutdownBridge::installed() const noexcept { return installed_; }

}  // namespace warehouse::bootstrap
