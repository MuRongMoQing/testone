#pragma once

#include "application/legacy/legacy_unit_of_work.hpp"
#include "infrastructure/mysql/connection_pool.hpp"

#include <chrono>

namespace warehouse::infrastructure::mysql {

class MySqlLegacyUnitOfWorkFactory final
    : public application::legacy::LegacyUnitOfWorkFactory {
public:
    explicit MySqlLegacyUnitOfWorkFactory(
        ConnectionPool& pool,
        std::chrono::milliseconds acquireTimeout = std::chrono::seconds(5)) noexcept;

    application::Result<std::unique_ptr<application::legacy::LegacyUnitOfWork>,
                        application::legacy::PersistenceFailure>
    begin(application::WorkUnitMode mode) override;

private:
    ConnectionPool& pool_;
    std::chrono::milliseconds acquireTimeout_;
};

}  // namespace warehouse::infrastructure::mysql
