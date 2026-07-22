#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace warehouse::bootstrap {

class ConfigurationSource {
public:
    virtual ~ConfigurationSource() = default;
    virtual std::optional<std::string> read(std::string_view key) const = 0;
};

class EnvironmentConfigurationSource final : public ConfigurationSource {
public:
    std::optional<std::string> read(std::string_view key) const override;
};

enum class ConfigurationIssueCode { Missing, Empty, InvalidInteger, OutOfRange, Inconsistent };

struct ConfigurationIssue {
    std::string key;
    ConfigurationIssueCode code = ConfigurationIssueCode::Missing;
    std::string message;
};

struct HttpServerConfiguration {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8081;
    std::size_t workerThreads = 1;
    std::vector<std::string> allowedOrigins;
};

struct MySqlConfiguration {
    std::string host;
    std::uint16_t port = 0;
    std::string database;
    std::string username;
    std::string password;
    std::size_t poolSize = 1;
};

struct LegacyStartupConfiguration {
    std::optional<std::string> initialAdministratorUsername;
    std::optional<std::string> initialAdministratorPassword;
    bool migratePlaintextPasswords = false;
};

struct ServerConfiguration {
    HttpServerConfiguration http;
    MySqlConfiguration mysql;
    LegacyStartupConfiguration legacy;
};

struct ConfigurationResult {
    std::optional<ServerConfiguration> configuration;
    std::vector<ConfigurationIssue> issues;
    bool ok() const noexcept;
};

ConfigurationResult loadServerConfiguration(const ConfigurationSource& source);

}  // namespace warehouse::bootstrap
