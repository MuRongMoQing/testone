#include "bootstrap/signal_shutdown.hpp"

#include <chrono>
#include <csignal>
#include <iostream>

namespace bootstrap = warehouse::bootstrap;

int main() {
    bootstrap::ShutdownCoordinator shutdown;
    {
        bootstrap::SignalShutdownBridge signals(shutdown,
                                                 std::chrono::milliseconds(1));
        if (!signals.installed()) {
            std::cerr << "FAILED: process signal handlers were not installed\n";
            return 1;
        }
        if (std::raise(SIGINT) != 0) {
            std::cerr << "FAILED: SIGINT could not be raised\n";
            return 1;
        }
        if (!shutdown.waitFor(std::chrono::milliseconds(500))) {
            std::cerr << "FAILED: SIGINT did not request orderly shutdown\n";
            return 1;
        }
    }
    return 0;
}
