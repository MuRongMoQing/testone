#pragma once

#include "application/common/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace warehouse::infrastructure::mysql {

enum class SqlErrorCode {
    PoolExhausted,
    Connection,
    ConnectionLost,
    Statement,
    Constraint,
    Deadlock,
    LockWaitTimeout,
    InvalidData,
    Migration,
    Unknown,
};

struct SqlError {
    SqlErrorCode code = SqlErrorCode::Unknown;
    unsigned int vendorCode = 0;
    std::string message;
};

using SqlCell = std::optional<std::string>;
using SqlRow = std::vector<SqlCell>;
using SqlValue = std::variant<std::nullptr_t, std::int64_t, std::uint64_t, std::string,
                              std::vector<unsigned char>>;

struct SqlResponse {
    std::vector<SqlRow> rows;
    std::uint64_t affectedRows = 0;
    std::uint64_t lastInsertId = 0;
};

using SqlResult = application::Result<SqlResponse, SqlError>;

class SqlExecutor {
public:
    virtual ~SqlExecutor() = default;
    // Only compile-time SQL and version-controlled migration SQL may use this seam.
    virtual SqlResult executeTrusted(std::string_view sql) = 0;
    virtual SqlResult executePrepared(
        std::string_view sql, const std::vector<SqlValue>& parameters) = 0;
    virtual bool reusable() const noexcept = 0;
    virtual void poison() noexcept = 0;
};

}  // namespace warehouse::infrastructure::mysql
