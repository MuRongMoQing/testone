#include "bootstrap/configuration.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace warehouse::bootstrap {
namespace {

std::string trim(std::string value) {
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

void clearSensitive(std::string& value) noexcept {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

void addIssue(ConfigurationResult& result, std::string key,
              ConfigurationIssueCode code, std::string message) {
    result.issues.push_back({std::move(key), code, std::move(message)});
}

std::optional<std::string> required(const ConfigurationSource& source,
                                    std::string_view key,
                                    ConfigurationResult& result) {
    auto value = source.read(key);
    if (!value) {
        addIssue(result, std::string(key), ConfigurationIssueCode::Missing,
                 "required configuration is missing");
        return std::nullopt;
    }
    if (value->empty()) {
        addIssue(result, std::string(key), ConfigurationIssueCode::Empty,
                 "required configuration is empty");
        return std::nullopt;
    }
    return value;
}

template <typename T>
std::optional<T> parsePositive(std::string_view key, const std::string& value,
                               T maximum, ConfigurationResult& result) {
    if (value.empty() || value.front() == '-' || value.front() == '+') {
        addIssue(result, std::string(key), ConfigurationIssueCode::InvalidInteger,
                 "configuration must be an unsigned decimal integer");
        return std::nullopt;
    }
    unsigned long long parsed = 0;
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size()) {
        addIssue(result, std::string(key), ConfigurationIssueCode::InvalidInteger,
                 "configuration must be an unsigned decimal integer");
        return std::nullopt;
    }
    if (parsed == 0 || parsed > static_cast<unsigned long long>(maximum)) {
        addIssue(result, std::string(key), ConfigurationIssueCode::OutOfRange,
                 "configuration integer is outside the allowed range");
        return std::nullopt;
    }
    return static_cast<T>(parsed);
}

template <typename T>
void optionalPositive(const ConfigurationSource& source, std::string_view key,
                      T maximum, T& target, ConfigurationResult& result) {
    const auto value = source.read(key);
    if (!value) return;
    if (value->empty()) {
        addIssue(result, std::string(key), ConfigurationIssueCode::Empty,
                 "configuration is empty");
        return;
    }
    const auto parsed = parsePositive<T>(key, *value, maximum, result);
    if (parsed) target = *parsed;
}

}  // namespace

std::optional<std::string> EnvironmentConfigurationSource::read(std::string_view key) const {
    const std::string ownedKey(key);
#ifdef _WIN32
    char* raw = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, ownedKey.c_str()) != 0 || raw == nullptr) {
        return std::nullopt;
    }
    std::string value(raw);
    std::fill(raw, raw + size, '\0');
    std::free(raw);
    return value;
#else
    const char* value = std::getenv(ownedKey.c_str());
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

bool ConfigurationResult::ok() const noexcept {
    return configuration.has_value() && issues.empty();
}

ConfigurationResult loadServerConfiguration(const ConfigurationSource& source) {
    ConfigurationResult result;
    ServerConfiguration configuration;

    const auto host = required(source, "WAREHOUSE_DB_HOST", result);
    const auto port = required(source, "WAREHOUSE_DB_PORT", result);
    const auto database = required(source, "WAREHOUSE_DB_NAME", result);
    const auto username = required(source, "WAREHOUSE_DB_USER", result);
    auto password = required(source, "WAREHOUSE_DB_PASSWORD", result);
    if (host) configuration.mysql.host = *host;
    if (database) configuration.mysql.database = *database;
    if (username) configuration.mysql.username = *username;
    if (password) configuration.mysql.password = *password;
    if (port) {
        const auto parsed = parsePositive<std::uint16_t>(
            "WAREHOUSE_DB_PORT", *port, std::numeric_limits<std::uint16_t>::max(), result);
        if (parsed) configuration.mysql.port = *parsed;
    }

    if (const auto bind = source.read("WAREHOUSE_HTTP_BIND_ADDRESS")) {
        if (bind->empty()) {
            addIssue(result, "WAREHOUSE_HTTP_BIND_ADDRESS", ConfigurationIssueCode::Empty,
                     "configuration is empty");
        } else {
            configuration.http.bindAddress = *bind;
        }
    }
    optionalPositive(source, "WAREHOUSE_HTTP_PORT",
                     std::numeric_limits<std::uint16_t>::max(), configuration.http.port, result);
    optionalPositive(source, "WAREHOUSE_HTTP_WORKER_THREADS",
                     std::numeric_limits<std::size_t>::max(),
                     configuration.http.workerThreads, result);
    optionalPositive(source, "WAREHOUSE_DB_POOL_SIZE",
                     std::numeric_limits<std::size_t>::max(), configuration.mysql.poolSize, result);

    if (const auto cors = source.read("WAREHOUSE_CORS_ALLOWED_ORIGINS")) {
        std::size_t start = 0;
        while (start <= cors->size()) {
            const auto comma = cors->find(',', start);
            auto origin = trim(cors->substr(start, comma == std::string::npos ? comma : comma - start));
            if (origin == "*") {
                addIssue(result, "WAREHOUSE_CORS_ALLOWED_ORIGINS",
                         ConfigurationIssueCode::Inconsistent,
                         "wildcard origin is not allowed");
            } else if (!origin.empty() &&
                       std::find(configuration.http.allowedOrigins.begin(),
                                 configuration.http.allowedOrigins.end(), origin) ==
                           configuration.http.allowedOrigins.end()) {
                configuration.http.allowedOrigins.push_back(std::move(origin));
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }

    configuration.legacy.initialAdministratorUsername =
        source.read("WAREHOUSE_ADMIN_USERNAME");
    configuration.legacy.initialAdministratorPassword =
        source.read("WAREHOUSE_ADMIN_PASSWORD");
    if (configuration.legacy.initialAdministratorUsername.has_value() !=
        configuration.legacy.initialAdministratorPassword.has_value()) {
        addIssue(result, "WAREHOUSE_ADMIN_USERNAME/WAREHOUSE_ADMIN_PASSWORD",
                 ConfigurationIssueCode::Inconsistent,
                 "initial administrator username and password must be supplied together");
    }
    if (const auto migrate = source.read("WAREHOUSE_MIGRATE_PASSWORDS")) {
        const auto normalized = lowercaseAscii(trim(*migrate));
        if (normalized == "1" || normalized == "true" || normalized == "yes") {
            configuration.legacy.migratePlaintextPasswords = true;
        } else if (normalized == "0" || normalized == "false" || normalized == "no") {
            configuration.legacy.migratePlaintextPasswords = false;
        } else {
            addIssue(result, "WAREHOUSE_MIGRATE_PASSWORDS",
                     ConfigurationIssueCode::Inconsistent,
                     "migration flag must be a boolean value");
        }
    }

    if (result.issues.empty()) {
        result.configuration = std::move(configuration);
    } else {
        clearSensitive(configuration.mysql.password);
        if (configuration.legacy.initialAdministratorPassword) {
            clearSensitive(*configuration.legacy.initialAdministratorPassword);
        }
    }
    if (password) clearSensitive(*password);
    return result;
}

}  // namespace warehouse::bootstrap
