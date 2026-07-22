#include "bootstrap/run_server.hpp"

#include "bootstrap/composition_root.hpp"
#include "bootstrap/configuration.hpp"
#include "bootstrap/production_platform.hpp"
#include "bootstrap/signal_shutdown.hpp"

#include <iostream>
#include <chrono>

namespace warehouse::bootstrap {

int runWarehouseServer() {
    EnvironmentConfigurationSource configuration;
    ProductionPlatform platform;
    CompositionRoot root(configuration, platform);
    ShutdownCoordinator shutdown;
    SignalShutdownBridge signals(shutdown);
    if (!signals.installed()) {
        std::cerr << "Process signal handlers could not be installed\n";
        return 1;
    }
    const auto result = root.start();
    if (!result.ok()) {
        std::cerr << "Startup failed [" << result.failure->code << "]: "
                  << result.failure->message << '\n';
        return 1;
    }
    if (!platform.listenerHealthy()) {
        std::cerr << "Startup failed [http_listener_not_ready]: "
                     "HTTP listener stopped before readiness was reported\n";
        root.stop();
        return 1;
    }
    std::cout << "Warehouse server is listening\n";
    while (!shutdown.waitFor(std::chrono::milliseconds(100))) {
        if (!platform.listenerHealthy()) {
            std::cerr << "Runtime failure [http_listener_stopped]: "
                         "HTTP listener stopped unexpectedly\n";
            root.stop();
            return 1;
        }
    }
    root.stop();
    return 0;
}

}  // namespace warehouse::bootstrap
