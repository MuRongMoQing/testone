#include "infrastructure/mysql/transaction.hpp"

#include <utility>

namespace warehouse::infrastructure::mysql {
namespace {

application::WorkUnitFailure failure(
    application::WorkUnitFailureCode code, const SqlError& error) {
    return {code,
            error.code == SqlErrorCode::Deadlock ||
                error.code == SqlErrorCode::LockWaitTimeout,
            error.message};
}

}  // namespace

application::Result<Transaction, application::WorkUnitFailure> Transaction::begin(
    SqlExecutor& executor, application::WorkUnitMode mode) {
    const char* isolation = mode == application::WorkUnitMode::FinalizedRead
        ? "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ"
        : "SET TRANSACTION ISOLATION LEVEL READ COMMITTED";
    auto isolated = executor.executeTrusted(isolation);
    if (!isolated) {
        executor.poison();
        return application::Result<Transaction, application::WorkUnitFailure>::failure(
            failure(application::WorkUnitFailureCode::BeginFailed, isolated.error()));
    }

    const char* start = "START TRANSACTION";
    if (mode == application::WorkUnitMode::ShortRead) {
        start = "START TRANSACTION READ ONLY";
    } else if (mode == application::WorkUnitMode::FinalizedRead) {
        start = "START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY";
    }
    auto started = executor.executeTrusted(start);
    if (!started) {
        executor.poison();
        return application::Result<Transaction, application::WorkUnitFailure>::failure(
            failure(application::WorkUnitFailureCode::BeginFailed, started.error()));
    }
    return application::Result<Transaction, application::WorkUnitFailure>::success(
        Transaction(executor));
}

Transaction::Transaction(Transaction&& other) noexcept
    : executor_(std::exchange(other.executor_, nullptr)),
      active_(std::exchange(other.active_, false)) {}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        rollback();
        executor_ = std::exchange(other.executor_, nullptr);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Transaction::~Transaction() { rollback(); }

application::Result<void, application::WorkUnitFailure> Transaction::commit() {
    if (!active_ || executor_ == nullptr) {
        return application::Result<void, application::WorkUnitFailure>::success();
    }
    auto result = executor_->executeTrusted("COMMIT");
    active_ = false;
    if (!result) {
        // Once COMMIT is sent, libmysql cannot prove whether the server committed.
        executor_->poison();
        return application::Result<void, application::WorkUnitFailure>::failure(
            failure(application::WorkUnitFailureCode::CommitOutcomeUnknown, result.error()));
    }
    return application::Result<void, application::WorkUnitFailure>::success();
}

void Transaction::rollback() noexcept {
    if (!active_ || executor_ == nullptr) return;
    auto result = executor_->executeTrusted("ROLLBACK");
    active_ = false;
    if (!result) executor_->poison();
}

}  // namespace warehouse::infrastructure::mysql
