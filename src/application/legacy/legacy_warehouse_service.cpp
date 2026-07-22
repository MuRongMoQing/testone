#include "application/legacy/legacy_warehouse_service.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace warehouse::application::legacy {
namespace {

LegacyApplicationError error(LegacyApplicationErrorCode code) { return {code}; }

std::string normalizeField(std::string value) {
    for (char& character : value) {
        if (character == '\t' || character == '\r' || character == '\n') character = ' ';
    }
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string lowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<std::int64_t> parseGoodsId(std::string supplied) {
    if (!supplied.empty() && (supplied.front() == 'G' || supplied.front() == 'g')) {
        supplied.erase(supplied.begin());
    }
    if (supplied.empty() || !std::all_of(supplied.begin(), supplied.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char character : supplied) {
        const auto digit = static_cast<unsigned int>(character - '0');
        if (value > (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    if (value == 0) return std::nullopt;
    return static_cast<std::int64_t>(value);
}

bool canWrite(const LegacyPrincipal& principal) {
    return principal.role == "manager" || principal.role == "admin";
}

GoodsView toView(const domain::legacy::GoodsRecord& record) {
    return {record.id, record.name, record.location, record.status, record.storedAt,
            record.takenAt, record.operatorName};
}

class PasswordClearGuard {
public:
    PasswordClearGuard(security::PasswordHasher& passwords, std::string& value)
        : passwords_(passwords), value_(value) {}
    ~PasswordClearGuard() { passwords_.clear(value_); }

private:
    security::PasswordHasher& passwords_;
    std::string& value_;
};

}  // namespace

LegacyWarehouseService::LegacyWarehouseService(LegacyUnitOfWorkFactory& units,
                                               LegacySessionStore& sessions,
                                               security::PasswordHasher& passwords)
    : units_(units), sessions_(sessions), passwords_(passwords) {}

Result<LoginView, LegacyApplicationError> LegacyWarehouseService::login(LoginCommand command) {
    PasswordClearGuard clear(passwords_, command.password);
    if (command.username.empty() || command.password.empty()) {
        return Result<LoginView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::InvalidLoginInput));
    }

    auto unitResult = units_.begin(WorkUnitMode::Command);
    if (!unitResult) {
        return Result<LoginView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceReadFailed));
    }
    auto unit = std::move(unitResult.value());
    auto userResult = unit->users().findByUsername(command.username, true);
    if (!userResult) {
        unit->rollback();
        return Result<LoginView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceReadFailed));
    }
    if (!userResult.value() ||
        !passwords_.verify(userResult.value()->passwordHash, command.password)) {
        unit->rollback();
        return Result<LoginView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::InvalidCredentials));
    }

    auto user = std::move(*userResult.value());
    if (passwords_.needsRehash(user.passwordHash)) {
        auto hashResult = passwords_.hash(command.password);
        if (!hashResult) {
            unit->rollback();
            return Result<LoginView, LegacyApplicationError>::failure(
                error(LegacyApplicationErrorCode::PasswordUpgradeFailed));
        }
        auto updateResult = unit->users().updatePasswordHash(user.id, hashResult.value());
        if (!updateResult) {
            unit->rollback();
            return Result<LoginView, LegacyApplicationError>::failure(
                error(LegacyApplicationErrorCode::PasswordUpgradeFailed));
        }
    }
    if (!unit->commit()) {
        return Result<LoginView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceReadFailed));
    }

    LegacyPrincipal principal{user.username, user.role};
    return Result<LoginView, LegacyApplicationError>::success(
        {sessions_.create(principal), user.role, user.username});
}

Result<std::vector<GoodsView>, LegacyApplicationError> LegacyWarehouseService::listGoods(
    ListGoodsQuery query) {
    const auto principal = sessions_.find(query.auth.value);
    if (!principal) {
        return Result<std::vector<GoodsView>, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::Unauthenticated));
    }
    auto unitResult = units_.begin(WorkUnitMode::ShortRead);
    if (!unitResult) {
        return Result<std::vector<GoodsView>, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceReadFailed));
    }
    auto unit = std::move(unitResult.value());
    domain::legacy::GoodsFilter filter{lowercaseAscii(std::move(query.name)), std::move(query.status)};
    auto rows = unit->goods().list(filter);
    if (!rows) {
        unit->rollback();
        return Result<std::vector<GoodsView>, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceReadFailed));
    }
    std::vector<GoodsView> views;
    views.reserve(rows.value().size());
    for (const auto& row : rows.value()) views.push_back(toView(row));
    if (!unit->commit()) {
        return Result<std::vector<GoodsView>, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceReadFailed));
    }
    return Result<std::vector<GoodsView>, LegacyApplicationError>::success(std::move(views));
}

Result<GoodsView, LegacyApplicationError> LegacyWarehouseService::createGoods(
    CreateGoodsCommand command) {
    const auto principal = sessions_.find(command.auth.value);
    if (!principal) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::Unauthenticated));
    }
    if (!canWrite(*principal)) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::Forbidden));
    }
    command.name = normalizeField(std::move(command.name));
    command.location = normalizeField(std::move(command.location));
    if (command.name.empty()) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::InvalidGoodsName));
    }
    if (command.location.empty()) command.location = "默认货架";

    auto unitResult = units_.begin(WorkUnitMode::Command);
    if (!unitResult) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceCreateFailed));
    }
    auto unit = std::move(unitResult.value());
    auto created = unit->goods().create({command.name, command.location, principal->username});
    if (!created) {
        unit->rollback();
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceCreateFailed));
    }
    if (!unit->commit()) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceCreateFailed));
    }
    return Result<GoodsView, LegacyApplicationError>::success(toView(created.value()));
}

Result<GoodsView, LegacyApplicationError> LegacyWarehouseService::takeGoods(
    TakeGoodsCommand command) {
    const auto principal = sessions_.find(command.auth.value);
    if (!principal) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::Unauthenticated));
    }
    if (!canWrite(*principal)) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::Forbidden));
    }
    const auto goodsId = parseGoodsId(std::move(command.suppliedId));
    if (!goodsId) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::InvalidGoodsId));
    }

    auto unitResult = units_.begin(WorkUnitMode::Command);
    if (!unitResult) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceTakeFailed));
    }
    auto unit = std::move(unitResult.value());
    auto outcome = unit->goods().markTaken(*goodsId, principal->username);
    if (!outcome) {
        unit->rollback();
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceTakeFailed));
    }
    if (outcome.value() == domain::legacy::TakeGoodsOutcome::NotFound) {
        unit->rollback();
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::GoodsNotFound));
    }
    if (outcome.value() == domain::legacy::TakeGoodsOutcome::AlreadyTaken) {
        unit->rollback();
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::GoodsAlreadyTaken));
    }
    auto record = unit->goods().findById(*goodsId);
    if (!record || !record.value()) {
        unit->rollback();
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceTakeFailed));
    }
    auto view = toView(*record.value());
    if (!unit->commit()) {
        return Result<GoodsView, LegacyApplicationError>::failure(
            error(LegacyApplicationErrorCode::PersistenceTakeFailed));
    }
    return Result<GoodsView, LegacyApplicationError>::success(std::move(view));
}

}  // namespace warehouse::application::legacy
