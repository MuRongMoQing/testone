#pragma once

#include "application/legacy/legacy_session_store.hpp"
#include "application/legacy/legacy_unit_of_work.hpp"
#include "application/legacy/legacy_warehouse_api.hpp"
#include "application/security/password_hasher.hpp"

namespace warehouse::application::legacy {

class LegacyWarehouseService final : public LegacyWarehouseApi {
public:
    LegacyWarehouseService(LegacyUnitOfWorkFactory& units,
                           LegacySessionStore& sessions,
                           security::PasswordHasher& passwords);

    Result<LoginView, LegacyApplicationError> login(LoginCommand command) override;
    Result<std::vector<GoodsView>, LegacyApplicationError> listGoods(
        ListGoodsQuery query) override;
    Result<GoodsView, LegacyApplicationError> createGoods(
        CreateGoodsCommand command) override;
    Result<GoodsView, LegacyApplicationError> takeGoods(
        TakeGoodsCommand command) override;

private:
    LegacyUnitOfWorkFactory& units_;
    LegacySessionStore& sessions_;
    security::PasswordHasher& passwords_;
};

}  // namespace warehouse::application::legacy
