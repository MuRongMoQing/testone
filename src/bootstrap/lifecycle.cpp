#include "bootstrap/lifecycle.hpp"

namespace warehouse::bootstrap {

bool StartupResult::ok() const noexcept { return !failure.has_value(); }

void ShutdownCoordinator::requestStop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    changed_.notify_all();
}

bool ShutdownCoordinator::stopRequested() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopRequested_;
}

void ShutdownCoordinator::wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [this] { return stopRequested_; });
}

bool ShutdownCoordinator::waitFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this] { return stopRequested_; });
}

}  // namespace warehouse::bootstrap
