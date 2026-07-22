#pragma once

#include "application/transactions/unit_of_work.hpp"
#include "infrastructure/mysql/executor.hpp"

namespace warehouse::infrastructure::mysql {

class Transaction final {
public:
    static application::Result<Transaction, application::WorkUnitFailure> begin(
        SqlExecutor& executor, application::WorkUnitMode mode);

    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&& other) noexcept;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    application::Result<void, application::WorkUnitFailure> commit();
    void rollback() noexcept;
    bool active() const noexcept { return active_; }

private:
    explicit Transaction(SqlExecutor& executor) noexcept : executor_(&executor), active_(true) {}
    SqlExecutor* executor_ = nullptr;
    bool active_ = false;
};

}  // namespace warehouse::infrastructure::mysql
