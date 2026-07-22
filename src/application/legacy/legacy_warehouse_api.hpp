#pragma once

#include "application/common/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace warehouse::application::legacy {

struct AuthToken { std::string value; };
struct LoginCommand { std::string username; std::string password; };
struct LoginView { std::string token; std::string role; std::string username; };

struct GoodsView {
    std::int64_t numericId = 0;
    std::string name;
    std::string location;
    std::string status;
    std::string storedAt;
    std::string takenAt;
    std::string operatorName;
};

struct ListGoodsQuery { AuthToken auth; std::string name; std::string status; };
struct CreateGoodsCommand { AuthToken auth; std::string name; std::string location; };
struct TakeGoodsCommand { AuthToken auth; std::string suppliedId; };

enum class LegacyApplicationErrorCode {
    InvalidLoginInput,
    InvalidCredentials,
    Unauthenticated,
    Forbidden,
    InvalidGoodsName,
    InvalidGoodsId,
    PersistenceReadFailed,
    PasswordUpgradeFailed,
    PersistenceCreateFailed,
    PersistenceTakeFailed,
    GoodsNotFound,
    GoodsAlreadyTaken,
};

struct LegacyApplicationError {
    LegacyApplicationErrorCode code = LegacyApplicationErrorCode::PersistenceReadFailed;
};

class LegacyWarehouseApi {
public:
    virtual ~LegacyWarehouseApi() = default;
    virtual Result<LoginView, LegacyApplicationError> login(LoginCommand command) = 0;
    virtual Result<std::vector<GoodsView>, LegacyApplicationError> listGoods(
        ListGoodsQuery query) = 0;
    virtual Result<GoodsView, LegacyApplicationError> createGoods(
        CreateGoodsCommand command) = 0;
    virtual Result<GoodsView, LegacyApplicationError> takeGoods(
        TakeGoodsCommand command) = 0;
};

}  // namespace warehouse::application::legacy
