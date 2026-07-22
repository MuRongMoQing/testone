#pragma once

#include "application/common/result.hpp"

#include <string>

namespace warehouse::application {

enum class WorkUnitMode {
    Command,
    ShortRead,
    FinalizedRead,
};

enum class WorkUnitFailureCode {
    BeginFailed,
    CommitFailed,
    CommitOutcomeUnknown,
    RollbackFailed,
};

struct WorkUnitFailure {
    WorkUnitFailureCode code = WorkUnitFailureCode::BeginFailed;
    bool retryable = false;
    std::string message;
};

class UnitOfWork {
public:
    virtual ~UnitOfWork() = default;
    virtual Result<void, WorkUnitFailure> commit() = 0;
    virtual void rollback() noexcept = 0;
    virtual bool active() const noexcept = 0;
};

}  // namespace warehouse::application
