#pragma once

#include "bootstrap/configuration.hpp"
#include "bootstrap/lifecycle.hpp"

namespace warehouse::infrastructure::mysql {
class ConnectionPool;
}

namespace warehouse::bootstrap {

StartupResult runLegacyStartupChecks(
    infrastructure::mysql::ConnectionPool& pool,
    const LegacyStartupConfiguration& configuration);

}  // namespace warehouse::bootstrap
