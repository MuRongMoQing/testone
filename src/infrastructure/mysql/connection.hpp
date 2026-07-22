#pragma once

#include "application/common/result.hpp"
#include "infrastructure/mysql/executor.hpp"

#include <memory>
#include <string>

namespace warehouse::infrastructure::mysql {

struct MySqlConnectionOptions {
    std::string host;
    unsigned int port = 3306;
    std::string database;
    std::string username;
    std::string password;
    unsigned int connectTimeoutSeconds = 10;
    unsigned int readTimeoutSeconds = 30;
    unsigned int writeTimeoutSeconds = 30;
};

class MySqlConnection final : public SqlExecutor {
public:
    static application::Result<std::unique_ptr<MySqlConnection>, SqlError> connect(
        const MySqlConnectionOptions& options);
    ~MySqlConnection() override;

    MySqlConnection(MySqlConnection&&) noexcept;
    MySqlConnection& operator=(MySqlConnection&&) noexcept;
    MySqlConnection(const MySqlConnection&) = delete;
    MySqlConnection& operator=(const MySqlConnection&) = delete;

    SqlResult executeTrusted(std::string_view sql) override;
    SqlResult executePrepared(std::string_view sql,
                              const std::vector<SqlValue>& parameters) override;
    bool reusable() const noexcept override;
    void poison() noexcept override;

private:
    class Impl;
    explicit MySqlConnection(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace warehouse::infrastructure::mysql
