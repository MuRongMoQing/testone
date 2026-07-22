#pragma once

#include "application/legacy/legacy_warehouse_api.hpp"

#include <functional>
#include <utility>

namespace warehouse::test_support {

class FakeLegacyWarehouseApi final : public application::legacy::LegacyWarehouseApi {
public:
    using LoginResult = application::Result<application::legacy::LoginView,
                                            application::legacy::LegacyApplicationError>;
    using GoodsListResult = application::Result<std::vector<application::legacy::GoodsView>,
                                                application::legacy::LegacyApplicationError>;
    using GoodsResult = application::Result<application::legacy::GoodsView,
                                           application::legacy::LegacyApplicationError>;

    std::function<LoginResult(application::legacy::LoginCommand)> onLogin =
        [](application::legacy::LoginCommand command) {
            return LoginResult::success({"token-1", "admin", std::move(command.username)});
        };
    std::function<GoodsListResult(application::legacy::ListGoodsQuery)> onListGoods =
        [](application::legacy::ListGoodsQuery) { return GoodsListResult::success({}); };
    std::function<GoodsResult(application::legacy::CreateGoodsCommand)> onCreateGoods =
        [](application::legacy::CreateGoodsCommand command) {
            return GoodsResult::success(
                {1, std::move(command.name), std::move(command.location), "stored",
                 "2026-07-19 10:00:00", "", "manager"});
        };
    std::function<GoodsResult(application::legacy::TakeGoodsCommand)> onTakeGoods =
        [](application::legacy::TakeGoodsCommand) {
            return GoodsResult::success(
                {1, "display", "A-01", "taken", "2026-07-19 10:00:00",
                 "2026-07-19 11:00:00", "manager"});
        };

    LoginResult login(application::legacy::LoginCommand command) override {
        return onLogin(std::move(command));
    }
    GoodsListResult listGoods(application::legacy::ListGoodsQuery query) override {
        return onListGoods(std::move(query));
    }
    GoodsResult createGoods(application::legacy::CreateGoodsCommand command) override {
        return onCreateGoods(std::move(command));
    }
    GoodsResult takeGoods(application::legacy::TakeGoodsCommand command) override {
        return onTakeGoods(std::move(command));
    }
};

}  // namespace warehouse::test_support
