#include "infrastructure/mysql/legacy_unit_of_work.hpp"

#include "infrastructure/mysql/transaction.hpp"

#include <charconv>
#include <memory>
#include <string>
#include <utility>

namespace warehouse::infrastructure::mysql {
namespace {

using application::legacy::PersistenceFailure;
using application::legacy::PersistenceFailureCode;

PersistenceFailure mapFailure(const SqlError& error) {
    PersistenceFailureCode code = PersistenceFailureCode::Unknown;
    switch (error.code) {
    case SqlErrorCode::PoolExhausted: code = PersistenceFailureCode::PoolExhausted; break;
    case SqlErrorCode::Connection:
    case SqlErrorCode::ConnectionLost: code = PersistenceFailureCode::Connection; break;
    case SqlErrorCode::Constraint: code = PersistenceFailureCode::Constraint; break;
    case SqlErrorCode::Deadlock: code = PersistenceFailureCode::Deadlock; break;
    case SqlErrorCode::LockWaitTimeout: code = PersistenceFailureCode::LockWaitTimeout; break;
    case SqlErrorCode::InvalidData: code = PersistenceFailureCode::InvalidData; break;
    case SqlErrorCode::Statement:
    case SqlErrorCode::Migration:
    case SqlErrorCode::Unknown: code = PersistenceFailureCode::Statement; break;
    }
    return {code,
            error.code == SqlErrorCode::Deadlock ||
                error.code == SqlErrorCode::LockWaitTimeout,
            error.message};
}

template <typename T>
bool parseInteger(const SqlCell& cell, T& output) {
    if (!cell) return false;
    const auto& text = *cell;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

application::Result<domain::legacy::GoodsRecord, PersistenceFailure> parseGoodsRecord(
    const SqlRow& row) {
    if (row.size() != 7 || !row[1] || !row[2] || !row[3] || !row[4] || !row[6]) {
        return application::Result<domain::legacy::GoodsRecord, PersistenceFailure>::failure(
            {PersistenceFailureCode::InvalidData, false,
             "legacy goods row has an invalid shape"});
    }
    domain::legacy::GoodsRecord record;
    if (!parseInteger(row[0], record.id)) {
        return application::Result<domain::legacy::GoodsRecord, PersistenceFailure>::failure(
            {PersistenceFailureCode::InvalidData, false, "legacy goods id is invalid"});
    }
    record.name = *row[1];
    record.location = *row[2];
    record.status = *row[3];
    record.storedAt = *row[4];
    record.takenAt = row[5].value_or("");
    record.operatorName = *row[6];
    return application::Result<domain::legacy::GoodsRecord, PersistenceFailure>::success(
        std::move(record));
}

class MySqlLegacyUsers final : public application::legacy::LegacyUsers {
public:
    explicit MySqlLegacyUsers(SqlExecutor& executor) : executor_(executor) {}

    application::Result<std::optional<domain::legacy::UserRecord>, PersistenceFailure>
    findByUsername(std::string_view username, bool forUpdate) override {
        const auto sql = forUpdate
            ? "SELECT id,username,password,role FROM users WHERE username=? FOR UPDATE"
            : "SELECT id,username,password,role FROM users WHERE username=?";
        auto result = executor_.executePrepared(sql, {std::string(username)});
        if (!result) return decltype(findByUsername(username, forUpdate))::failure(
            mapFailure(result.error()));
        if (result.value().rows.empty()) {
            return decltype(findByUsername(username, forUpdate))::success(std::nullopt);
        }
        const auto& row = result.value().rows.front();
        domain::legacy::UserRecord user;
        if (row.size() != 4 || !parseInteger(row[0], user.id) || !row[1] || !row[2] || !row[3]) {
            return decltype(findByUsername(username, forUpdate))::failure(
                {PersistenceFailureCode::InvalidData, false,
                 "legacy user row has an invalid shape"});
        }
        user.username = *row[1];
        user.passwordHash = *row[2];
        user.role = *row[3];
        return decltype(findByUsername(username, forUpdate))::success(std::move(user));
    }

    application::Result<void, PersistenceFailure> updatePasswordHash(
        std::uint64_t userId, std::string_view passwordHash) override {
        auto result = executor_.executePrepared(
            "UPDATE users SET password=? WHERE id=?", {std::string(passwordHash), userId});
        if (!result) return application::Result<void, PersistenceFailure>::failure(
            mapFailure(result.error()));
        if (result.value().affectedRows != 1) {
            return application::Result<void, PersistenceFailure>::failure(
                {PersistenceFailureCode::InvalidData, false,
                 "password update affected an unexpected number of rows"});
        }
        return application::Result<void, PersistenceFailure>::success();
    }

    application::Result<std::uint64_t, PersistenceFailure> countAdministrators() override {
        auto result = executor_.executePrepared(
            "SELECT COUNT(*) FROM users WHERE role='admin'", {});
        if (!result) return application::Result<std::uint64_t, PersistenceFailure>::failure(
            mapFailure(result.error()));
        std::uint64_t count = 0;
        if (result.value().rows.size() != 1 || result.value().rows.front().size() != 1 ||
            !parseInteger(result.value().rows.front().front(), count)) {
            return application::Result<std::uint64_t, PersistenceFailure>::failure(
                {PersistenceFailureCode::InvalidData, false,
                 "administrator count result is invalid"});
        }
        return application::Result<std::uint64_t, PersistenceFailure>::success(count);
    }

    application::Result<void, PersistenceFailure> createAdministrator(
        std::string_view username, std::string_view passwordHash) override {
        auto result = executor_.executePrepared(
            "INSERT INTO users(username,password,role) VALUES(?,?,'admin')",
            {std::string(username), std::string(passwordHash)});
        if (!result) return application::Result<void, PersistenceFailure>::failure(
            mapFailure(result.error()));
        return application::Result<void, PersistenceFailure>::success();
    }

private:
    SqlExecutor& executor_;
};

class MySqlLegacyGoods final : public application::legacy::LegacyGoods {
public:
    explicit MySqlLegacyGoods(SqlExecutor& executor) : executor_(executor) {}

    application::Result<std::vector<domain::legacy::GoodsRecord>, PersistenceFailure> list(
        const domain::legacy::GoodsFilter& filter) override {
        std::string sql =
            "SELECT id,name,location,status,"
            "DATE_FORMAT(stored_at,'%Y-%m-%d %H:%i:%s'),"
            "DATE_FORMAT(taken_at,'%Y-%m-%d %H:%i:%s'),operator FROM goods WHERE 1=1";
        std::vector<SqlValue> parameters;
        if (!filter.name.empty()) {
            sql += " AND LOWER(name) LIKE CONCAT('%',?,'%')";
            parameters.emplace_back(filter.name);
        }
        if (!filter.status.empty()) {
            sql += " AND status=?";
            parameters.emplace_back(filter.status);
        }
        sql += " ORDER BY id";
        auto result = executor_.executePrepared(sql, parameters);
        if (!result) return decltype(list(filter))::failure(mapFailure(result.error()));
        std::vector<domain::legacy::GoodsRecord> records;
        records.reserve(result.value().rows.size());
        for (const auto& row : result.value().rows) {
            auto parsed = parseGoodsRecord(row);
            if (!parsed) return decltype(list(filter))::failure(parsed.error());
            records.push_back(std::move(parsed.value()));
        }
        return decltype(list(filter))::success(std::move(records));
    }

    application::Result<domain::legacy::GoodsRecord, PersistenceFailure> create(
        const domain::legacy::NewGoods& goods) override {
        auto inserted = executor_.executePrepared(
            "INSERT INTO goods(name,location,status,stored_at,operator) "
            "VALUES(?,?,'stored',UTC_TIMESTAMP(),?)",
            {goods.name, goods.location, goods.operatorName});
        if (!inserted) return decltype(create(goods))::failure(mapFailure(inserted.error()));
        auto found = findById(static_cast<std::int64_t>(inserted.value().lastInsertId));
        if (!found) return decltype(create(goods))::failure(found.error());
        if (!found.value()) return decltype(create(goods))::failure(
            {PersistenceFailureCode::InvalidData, false,
             "created goods row could not be read back"});
        return decltype(create(goods))::success(std::move(*found.value()));
    }

    application::Result<std::optional<domain::legacy::GoodsRecord>, PersistenceFailure>
    findById(std::int64_t goodsId) override {
        auto result = executor_.executePrepared(
            "SELECT id,name,location,status,"
            "DATE_FORMAT(stored_at,'%Y-%m-%d %H:%i:%s'),"
            "DATE_FORMAT(taken_at,'%Y-%m-%d %H:%i:%s'),operator FROM goods WHERE id=?",
            {goodsId});
        if (!result) return decltype(findById(goodsId))::failure(mapFailure(result.error()));
        if (result.value().rows.empty()) return decltype(findById(goodsId))::success(std::nullopt);
        auto parsed = parseGoodsRecord(result.value().rows.front());
        if (!parsed) return decltype(findById(goodsId))::failure(parsed.error());
        return decltype(findById(goodsId))::success(std::move(parsed.value()));
    }

    application::Result<domain::legacy::TakeGoodsOutcome, PersistenceFailure> markTaken(
        std::int64_t goodsId, std::string_view operatorName) override {
        auto updated = executor_.executePrepared(
            "UPDATE goods SET status='taken',taken_at=UTC_TIMESTAMP(),operator=? "
            "WHERE id=? AND status<>'taken'",
            {std::string(operatorName), goodsId});
        if (!updated) return decltype(markTaken(goodsId, operatorName))::failure(
            mapFailure(updated.error()));
        if (updated.value().affectedRows == 1) {
            return decltype(markTaken(goodsId, operatorName))::success(
                domain::legacy::TakeGoodsOutcome::Taken);
        }
        auto status = executor_.executePrepared(
            "SELECT status FROM goods WHERE id=? FOR UPDATE", {goodsId});
        if (!status) return decltype(markTaken(goodsId, operatorName))::failure(
            mapFailure(status.error()));
        if (status.value().rows.empty()) {
            return decltype(markTaken(goodsId, operatorName))::success(
                domain::legacy::TakeGoodsOutcome::NotFound);
        }
        return decltype(markTaken(goodsId, operatorName))::success(
            domain::legacy::TakeGoodsOutcome::AlreadyTaken);
    }

private:
    SqlExecutor& executor_;
};

class MySqlLegacyUnitOfWork final : public application::legacy::LegacyUnitOfWork {
public:
    MySqlLegacyUnitOfWork(ConnectionLease lease, Transaction transaction)
        : lease_(std::move(lease)), transaction_(std::move(transaction)),
          users_(lease_.executor()), goods_(lease_.executor()) {}

    application::legacy::LegacyUsers& users() noexcept override { return users_; }
    application::legacy::LegacyGoods& goods() noexcept override { return goods_; }
    application::Result<void, application::WorkUnitFailure> commit() override {
        return transaction_.commit();
    }
    void rollback() noexcept override { transaction_.rollback(); }
    bool active() const noexcept override { return transaction_.active(); }

private:
    ConnectionLease lease_;
    Transaction transaction_;
    MySqlLegacyUsers users_;
    MySqlLegacyGoods goods_;
};

}  // namespace

MySqlLegacyUnitOfWorkFactory::MySqlLegacyUnitOfWorkFactory(
    ConnectionPool& pool, std::chrono::milliseconds acquireTimeout) noexcept
    : pool_(pool), acquireTimeout_(acquireTimeout) {}

application::Result<std::unique_ptr<application::legacy::LegacyUnitOfWork>,
                    application::legacy::PersistenceFailure>
MySqlLegacyUnitOfWorkFactory::begin(application::WorkUnitMode mode) {
    auto lease = pool_.acquire(acquireTimeout_);
    if (!lease) return decltype(begin(mode))::failure(mapFailure(lease.error()));
    auto transaction = Transaction::begin(lease.value().executor(), mode);
    if (!transaction) {
        return decltype(begin(mode))::failure(
            {PersistenceFailureCode::Connection, transaction.error().retryable,
             transaction.error().message});
    }
    std::unique_ptr<application::legacy::LegacyUnitOfWork> unit =
        std::make_unique<MySqlLegacyUnitOfWork>(
            std::move(lease.value()), std::move(transaction.value()));
    return decltype(begin(mode))::success(std::move(unit));
}

}  // namespace warehouse::infrastructure::mysql
