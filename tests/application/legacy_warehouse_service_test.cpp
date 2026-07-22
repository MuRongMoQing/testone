#include "application/legacy/legacy_warehouse_service.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace app = warehouse::application;
namespace legacy = warehouse::application::legacy;
namespace model = warehouse::domain::legacy;

namespace {

struct FakeUsers final : legacy::LegacyUsers {
    std::optional<model::UserRecord> user;
    std::string updatedHash;

    app::Result<std::optional<model::UserRecord>, legacy::PersistenceFailure>
    findByUsername(std::string_view username, bool) override {
        if (user && user->username == username) return decltype(findByUsername(username, false))::success(user);
        return decltype(findByUsername(username, false))::success(std::nullopt);
    }

    app::Result<void, legacy::PersistenceFailure> updatePasswordHash(
        std::uint64_t, std::string_view passwordHash) override {
        updatedHash.assign(passwordHash);
        return app::Result<void, legacy::PersistenceFailure>::success();
    }

    app::Result<std::uint64_t, legacy::PersistenceFailure> countAdministrators() override {
        return app::Result<std::uint64_t, legacy::PersistenceFailure>::success(1);
    }

    app::Result<void, legacy::PersistenceFailure> createAdministrator(
        std::string_view, std::string_view) override {
        return app::Result<void, legacy::PersistenceFailure>::success();
    }
};

struct FakeGoods final : legacy::LegacyGoods {
    std::vector<model::GoodsRecord> rows;
    model::GoodsFilter lastFilter;
    model::NewGoods lastCreated;
    model::TakeGoodsOutcome takeOutcome = model::TakeGoodsOutcome::Taken;

    app::Result<std::vector<model::GoodsRecord>, legacy::PersistenceFailure> list(
        const model::GoodsFilter& filter) override {
        lastFilter = filter;
        return decltype(list(filter))::success(rows);
    }

    app::Result<model::GoodsRecord, legacy::PersistenceFailure> create(
        const model::NewGoods& goods) override {
        lastCreated = goods;
        model::GoodsRecord record{7, goods.name, goods.location, "stored",
                                  "2026-07-19 10:00:00", "", goods.operatorName};
        rows = {record};
        return decltype(create(goods))::success(record);
    }

    app::Result<std::optional<model::GoodsRecord>, legacy::PersistenceFailure> findById(
        std::int64_t goodsId) override {
        for (const auto& row : rows) {
            if (row.id == goodsId) return decltype(findById(goodsId))::success(row);
        }
        return decltype(findById(goodsId))::success(std::nullopt);
    }

    app::Result<model::TakeGoodsOutcome, legacy::PersistenceFailure> markTaken(
        std::int64_t goodsId, std::string_view operatorName) override {
        if (takeOutcome == model::TakeGoodsOutcome::Taken) {
            for (auto& row : rows) {
                if (row.id == goodsId) {
                    row.status = "taken";
                    row.takenAt = "2026-07-19 11:00:00";
                    row.operatorName.assign(operatorName);
                }
            }
        }
        return decltype(markTaken(goodsId, operatorName))::success(takeOutcome);
    }
};

struct FakeUnitOfWork final : legacy::LegacyUnitOfWork {
    FakeUnitOfWork(FakeUsers& users, FakeGoods& goods, int& commits, int& rollbacks)
        : users_(users), goods_(goods), commits_(commits), rollbacks_(rollbacks) {}
    ~FakeUnitOfWork() override { if (active_) rollback(); }

    legacy::LegacyUsers& users() noexcept override { return users_; }
    legacy::LegacyGoods& goods() noexcept override { return goods_; }
    app::Result<void, app::WorkUnitFailure> commit() override {
        active_ = false;
        ++commits_;
        return app::Result<void, app::WorkUnitFailure>::success();
    }
    void rollback() noexcept override {
        if (active_) {
            active_ = false;
            ++rollbacks_;
        }
    }
    bool active() const noexcept override { return active_; }

    FakeUsers& users_;
    FakeGoods& goods_;
    int& commits_;
    int& rollbacks_;
    bool active_ = true;
};

struct FakeFactory final : legacy::LegacyUnitOfWorkFactory {
    FakeUsers users;
    FakeGoods goods;
    app::WorkUnitMode lastMode = app::WorkUnitMode::ShortRead;
    int commits = 0;
    int rollbacks = 0;

    app::Result<std::unique_ptr<legacy::LegacyUnitOfWork>, legacy::PersistenceFailure> begin(
        app::WorkUnitMode mode) override {
        lastMode = mode;
        std::unique_ptr<legacy::LegacyUnitOfWork> unit =
            std::make_unique<FakeUnitOfWork>(users, goods, commits, rollbacks);
        return decltype(begin(mode))::success(std::move(unit));
    }
};

struct FakeSessions final : legacy::LegacySessionStore {
    std::map<std::string, legacy::LegacyPrincipal> sessions;
    std::string create(const legacy::LegacyPrincipal& principal) override {
        sessions["issued-token"] = principal;
        return "issued-token";
    }
    std::optional<legacy::LegacyPrincipal> find(std::string_view token) const override {
        const auto it = sessions.find(std::string(token));
        return it == sessions.end() ? std::nullopt : std::optional<legacy::LegacyPrincipal>(it->second);
    }
};

struct FakePasswords final : app::security::PasswordHasher {
    bool rehash = false;
    bool verify(std::string_view hash, std::string_view password) const override {
        return hash == "stored-hash" && password == "secret";
    }
    bool needsRehash(std::string_view) const override { return rehash; }
    app::Result<std::string, std::string> hash(std::string_view) const override {
        return app::Result<std::string, std::string>::success("new-hash");
    }
    void clear(std::string& sensitive) const noexcept override { sensitive.clear(); }
};

int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    FakeFactory factory;
    FakeSessions sessions;
    FakePasswords passwords;
    legacy::LegacyWarehouseService service(factory, sessions, passwords);

    factory.users.user = model::UserRecord{1, "manager", "stored-hash", "manager"};
    passwords.rehash = true;
    auto login = service.login({"manager", "secret"});
    expect(login && login.value().token == "issued-token", "login returns issued token");
    expect(login && login.value().role == "manager", "login returns legacy role");
    expect(factory.users.updatedHash == "new-hash", "login upgrades password hash");
    expect(factory.lastMode == app::WorkUnitMode::Command, "login uses command work unit");

    sessions.sessions["viewer-token"] = {"viewer", "viewer"};
    factory.goods.rows = {{42, "Widget", "A-1", "stored", "2026-07-19 10:00:00", "", "manager"}};
    auto listed = service.listGoods({{"viewer-token"}, "WID", "stored"});
    expect(listed && listed.value().size() == 1, "authenticated user lists goods");
    expect(factory.goods.lastFilter.name == "wid", "list filter is case-normalized");
    expect(factory.lastMode == app::WorkUnitMode::ShortRead, "list uses short read work unit");

    sessions.sessions["manager-token"] = {"manager", "manager"};
    auto created = service.createGoods({{"manager-token"}, "\tWidget\n", "   "});
    expect(created && created.value().numericId == 7, "manager creates goods");
    expect(factory.goods.lastCreated.name == "Widget", "goods name is normalized");
    expect(factory.goods.lastCreated.location == "默认货架", "empty location uses legacy default");

    auto forbidden = service.createGoods({{"viewer-token"}, "Widget", "A-1"});
    expect(!forbidden && forbidden.error().code == legacy::LegacyApplicationErrorCode::Forbidden,
           "viewer cannot create goods");

    factory.goods.rows = {{42, "Widget", "A-1", "stored", "2026-07-19 10:00:00", "", "manager"}};
    auto taken = service.takeGoods({{"manager-token"}, "g000042"});
    expect(taken && taken.value().status == "taken", "prefixed id can be taken");
    expect(taken && taken.value().operatorName == "manager", "take records operator");

    auto invalidId = service.takeGoods({{"manager-token"}, "G0x"});
    expect(!invalidId && invalidId.error().code == legacy::LegacyApplicationErrorCode::InvalidGoodsId,
           "invalid goods id is rejected");

    auto unauthenticated = service.listGoods({{"missing"}, "", ""});
    expect(!unauthenticated &&
               unauthenticated.error().code == legacy::LegacyApplicationErrorCode::Unauthenticated,
           "unknown token is rejected");

    return failures == 0 ? 0 : 1;
}
