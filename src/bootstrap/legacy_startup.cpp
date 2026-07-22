#include "bootstrap/legacy_startup.hpp"

#include "infrastructure/mysql/connection_pool.hpp"
#include "infrastructure/mysql/transaction.hpp"
#include "password_hash.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <string>

namespace warehouse::bootstrap {
namespace {

StartupResult failure(std::string code, std::string message) {
    return StartupResult{StartupFailure{
        StartupStage::LegacyStartupCheck, std::move(code), std::move(message)}};
}

std::string normalizeUsername(std::string value) {
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

bool parseId(const infrastructure::mysql::SqlCell& cell, std::uint64_t& id) {
    if (!cell) return false;
    const auto& value = *cell;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), id);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

}  // namespace

StartupResult runLegacyStartupChecks(
    infrastructure::mysql::ConnectionPool& pool,
    const LegacyStartupConfiguration& configuration) {
    auto lease = pool.acquire(std::chrono::seconds(5));
    if (!lease) return failure("database_unavailable", lease.error().message);
    auto transaction = infrastructure::mysql::Transaction::begin(
        lease.value().executor(), application::WorkUnitMode::Command);
    if (!transaction) return failure("transaction_start_failed", transaction.error().message);

    auto passwords = lease.value().executor().executePrepared(
        "SELECT id,password FROM users FOR UPDATE", {});
    if (!passwords) {
        transaction.value().rollback();
        return failure("password_scan_failed", passwords.error().message);
    }
    for (const auto& row : passwords.value().rows) {
        std::uint64_t userId = 0;
        if (row.size() != 2 || !parseId(row[0], userId) || !row[1]) {
            transaction.value().rollback();
            return failure("invalid_password_row", "stored password row is invalid");
        }
        std::string stored = *row[1];
        const auto kind = security::classifyStoredPassword(stored);
        if (kind == security::StoredPasswordKind::InvalidPasswordHash) {
            security::clearSensitiveString(stored);
            transaction.value().rollback();
            return failure("invalid_password_hash", "an invalid Argon2 password hash is stored");
        }
        if (kind == security::StoredPasswordKind::LegacyPlaintext) {
            if (!configuration.migratePlaintextPasswords) {
                security::clearSensitiveString(stored);
                transaction.value().rollback();
                return failure("plaintext_passwords_present",
                               "legacy plaintext passwords require explicit migration authorization");
            }
            std::string hash;
            std::string hashingError;
            if (!security::hashPassword(stored, hash, hashingError)) {
                security::clearSensitiveString(stored);
                transaction.value().rollback();
                return failure("password_hash_failed", hashingError);
            }
            security::clearSensitiveString(stored);
            auto updated = lease.value().executor().executePrepared(
                "UPDATE users SET password=? WHERE id=?", {hash, userId});
            security::clearSensitiveString(hash);
            if (!updated || updated.value().affectedRows != 1) {
                transaction.value().rollback();
                return failure("password_migration_failed",
                               updated ? "password migration affected an unexpected row count"
                                       : updated.error().message);
            }
        } else {
            security::clearSensitiveString(stored);
        }
    }

    auto adminCount = lease.value().executor().executePrepared(
        "SELECT COUNT(*) FROM users WHERE role='admin'", {});
    std::uint64_t count = 0;
    if (!adminCount || adminCount.value().rows.size() != 1 ||
        adminCount.value().rows.front().size() != 1 ||
        !parseId(adminCount.value().rows.front().front(), count)) {
        transaction.value().rollback();
        return failure("administrator_check_failed",
                       adminCount ? "administrator count result is invalid"
                                  : adminCount.error().message);
    }
    if (count == 0) {
        if (!configuration.initialAdministratorUsername ||
            !configuration.initialAdministratorPassword) {
            transaction.value().rollback();
            return failure("initial_administrator_required",
                           "initial administrator credentials are required");
        }
        auto username = normalizeUsername(*configuration.initialAdministratorUsername);
        auto password = *configuration.initialAdministratorPassword;
        if (username.empty() || username.size() > 64 || password.size() < 12 ||
            password.size() > 128) {
            security::clearSensitiveString(password);
            transaction.value().rollback();
            return failure("invalid_initial_administrator",
                           "initial administrator credentials do not meet length requirements");
        }
        std::string hash;
        std::string hashingError;
        if (!security::hashPassword(password, hash, hashingError)) {
            security::clearSensitiveString(password);
            transaction.value().rollback();
            return failure("administrator_password_hash_failed", hashingError);
        }
        security::clearSensitiveString(password);
        auto inserted = lease.value().executor().executePrepared(
            "INSERT INTO users(username,password,role) VALUES(?,?,'admin')",
            {username, hash});
        security::clearSensitiveString(hash);
        if (!inserted) {
            transaction.value().rollback();
            return failure("administrator_create_failed", inserted.error().message);
        }
    }

    auto committed = transaction.value().commit();
    if (!committed) return failure("legacy_startup_commit_failed", committed.error().message);
    return {};
}

}  // namespace warehouse::bootstrap
