#include "bootstrap/lifecycle.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace bootstrap = warehouse::bootstrap;

int main() {
    int failures = 0;
    const auto expect = [&failures](bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    };

    bootstrap::ShutdownCoordinator shutdown;
    expect(!shutdown.stopRequested(), "shutdown begins clear");
    expect(!shutdown.waitFor(std::chrono::milliseconds(1)), "wait times out before request");

    bool awakened = false;
    std::thread waiter([&] {
        shutdown.wait();
        awakened = true;
    });
    shutdown.requestStop();
    shutdown.requestStop();
    waiter.join();
    expect(awakened, "request wakes waiters");
    expect(shutdown.stopRequested(), "request is observable");
    expect(shutdown.waitFor(std::chrono::milliseconds(1)), "wait returns immediately after request");

    bootstrap::StartupResult success;
    expect(success.ok(), "startup without failure succeeds");
    bootstrap::StartupResult failed{
        bootstrap::StartupFailure{bootstrap::StartupStage::Migration,
                                  "migration_failed", "migration did not complete"}};
    expect(!failed.ok(), "structured startup failure is not success");
    return failures == 0 ? 0 : 1;
}
