#pragma once

#include "application/common/result.hpp"
#include "application/transactions/unit_of_work.hpp"
#include "domain/legacy/legacy_models.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace warehouse::application::legacy {

enum class PersistenceFailureCode {
    PoolExhausted,
    Connection,
    Statement,
    Constraint,
    Deadlock,
    LockWaitTimeout,
    InvalidData,
    Unknown,
};

struct PersistenceFailure {
    PersistenceFailureCode code = PersistenceFailureCode::Unknown;
    bool retryable = false;
    std::string message;
};

class LegacyUsers {
public:
    virtual ~LegacyUsers() = default;
    virtual Result<std::optional<domain::legacy::UserRecord>, PersistenceFailure>
        findByUsername(std::string_view username, bool forUpdate) = 0;
    virtual Result<void, PersistenceFailure> updatePasswordHash(
        std::uint64_t userId, std::string_view passwordHash) = 0;
    virtual Result<std::uint64_t, PersistenceFailure> countAdministrators() = 0;
    virtual Result<void, PersistenceFailure> createAdministrator(
        std::string_view username, std::string_view passwordHash) = 0;
};

class LegacyGoods {
public:
    virtual ~LegacyGoods() = default;
    virtual Result<std::vector<domain::legacy::GoodsRecord>, PersistenceFailure>
        list(const domain::legacy::GoodsFilter& filter) = 0;
    virtual Result<domain::legacy::GoodsRecord, PersistenceFailure> create(
        const domain::legacy::NewGoods& goods) = 0;
    virtual Result<std::optional<domain::legacy::GoodsRecord>, PersistenceFailure>
        findById(std::int64_t goodsId) = 0;
    virtual Result<domain::legacy::TakeGoodsOutcome, PersistenceFailure> markTaken(
        std::int64_t goodsId, std::string_view operatorName) = 0;
};

class LegacyUnitOfWork : public UnitOfWork {
public:
    ~LegacyUnitOfWork() override = default;
    virtual LegacyUsers& users() noexcept = 0;
    virtual LegacyGoods& goods() noexcept = 0;
};

class LegacyUnitOfWorkFactory {
public:
    virtual ~LegacyUnitOfWorkFactory() = default;
    virtual Result<std::unique_ptr<LegacyUnitOfWork>, PersistenceFailure> begin(
        WorkUnitMode mode) = 0;
};

}  // namespace warehouse::application::legacy
