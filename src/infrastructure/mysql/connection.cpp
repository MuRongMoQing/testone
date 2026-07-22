#include "infrastructure/mysql/connection.hpp"

#include "infrastructure/mysql/client_library.hpp"

#ifdef _WIN32
#include <mysql.h>
#else
#include <mysql/mysql.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace warehouse::infrastructure::mysql {
namespace {

SqlErrorCode classify(unsigned int code) {
    switch (code) {
    case 1205: return SqlErrorCode::LockWaitTimeout;
    case 1213: return SqlErrorCode::Deadlock;
    case 1062:
    case 1451:
    case 1452: return SqlErrorCode::Constraint;
    case 2006:
    case 2013: return SqlErrorCode::ConnectionLost;
    default: return SqlErrorCode::Statement;
    }
}

SqlError connectionError(MYSQL* connection) {
    const auto code = connection ? mysql_errno(connection) : 0;
    return {classify(code), code,
            connection ? mysql_error(connection) : "mysql connection is unavailable"};
}

SqlError statementError(MYSQL_STMT* statement) {
    const auto code = statement ? mysql_stmt_errno(statement) : 0;
    return {classify(code), code,
            statement ? mysql_stmt_error(statement) : "mysql statement is unavailable"};
}

bool connectionLost(const SqlError& error) {
    return error.code == SqlErrorCode::ConnectionLost || error.code == SqlErrorCode::Connection;
}

class StatementGuard {
public:
    explicit StatementGuard(MYSQL_STMT* statement) : statement_(statement) {}
    ~StatementGuard() {
        if (statement_) mysql_stmt_close(statement_);
    }
    MYSQL_STMT* get() const noexcept { return statement_; }

private:
    MYSQL_STMT* statement_;
};

}  // namespace

class MySqlConnection::Impl {
public:
    explicit Impl(MYSQL* connection) : connection(connection) {}
    ~Impl() {
        if (connection) mysql_close(connection);
    }

    MYSQL* connection = nullptr;
    bool poisoned = false;
};

MySqlConnection::MySqlConnection(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
MySqlConnection::~MySqlConnection() = default;
MySqlConnection::MySqlConnection(MySqlConnection&&) noexcept = default;
MySqlConnection& MySqlConnection::operator=(MySqlConnection&&) noexcept = default;

application::Result<std::unique_ptr<MySqlConnection>, SqlError> MySqlConnection::connect(
    const MySqlConnectionOptions& options) {
    if (!ensureThreadContext()) {
        return application::Result<std::unique_ptr<MySqlConnection>, SqlError>::failure(
            {SqlErrorCode::Connection, 0, "mysql thread initialization failed"});
    }
    MYSQL* raw = mysql_init(nullptr);
    if (!raw) {
        return application::Result<std::unique_ptr<MySqlConnection>, SqlError>::failure(
            {SqlErrorCode::Connection, 0, "mysql_init failed"});
    }
    std::unique_ptr<Impl> impl = std::make_unique<Impl>(raw);
    bool reconnect = false;
    mysql_options(raw, MYSQL_OPT_RECONNECT, &reconnect);
    mysql_options(raw, MYSQL_OPT_CONNECT_TIMEOUT, &options.connectTimeoutSeconds);
    mysql_options(raw, MYSQL_OPT_READ_TIMEOUT, &options.readTimeoutSeconds);
    mysql_options(raw, MYSQL_OPT_WRITE_TIMEOUT, &options.writeTimeoutSeconds);

    if (!mysql_real_connect(raw, options.host.c_str(), options.username.c_str(),
                            options.password.c_str(), options.database.c_str(), options.port,
                            nullptr, 0)) {
        auto error = connectionError(raw);
        error.code = SqlErrorCode::Connection;
        return application::Result<std::unique_ptr<MySqlConnection>, SqlError>::failure(
            std::move(error));
    }
    if (mysql_set_character_set(raw, "utf8mb4") != 0) {
        return application::Result<std::unique_ptr<MySqlConnection>, SqlError>::failure(
            connectionError(raw));
    }

    auto connection = std::unique_ptr<MySqlConnection>(
        new MySqlConnection(std::move(impl)));
    auto utc = connection->executeTrusted("SET time_zone = '+00:00'");
    if (!utc) {
        return application::Result<std::unique_ptr<MySqlConnection>, SqlError>::failure(
            utc.error());
    }
    auto strict = connection->executeTrusted(
        "SET SESSION sql_mode = 'STRICT_TRANS_TABLES,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION'");
    if (!strict) {
        return application::Result<std::unique_ptr<MySqlConnection>, SqlError>::failure(
            strict.error());
    }
    return application::Result<std::unique_ptr<MySqlConnection>, SqlError>::success(
        std::move(connection));
}

SqlResult MySqlConnection::executeTrusted(std::string_view sql) {
    if (!impl_ || !impl_->connection || impl_->poisoned || !ensureThreadContext()) {
        return SqlResult::failure(
            {SqlErrorCode::Connection, 0, "mysql connection is not reusable"});
    }
    if (mysql_real_query(impl_->connection, sql.data(),
                         static_cast<unsigned long>(sql.size())) != 0) {
        auto error = connectionError(impl_->connection);
        if (connectionLost(error)) impl_->poisoned = true;
        return SqlResult::failure(std::move(error));
    }

    SqlResponse response;
    response.affectedRows = mysql_affected_rows(impl_->connection);
    response.lastInsertId = mysql_insert_id(impl_->connection);
    MYSQL_RES* result = mysql_store_result(impl_->connection);
    if (!result) {
        if (mysql_field_count(impl_->connection) != 0) {
            auto error = connectionError(impl_->connection);
            if (connectionLost(error)) impl_->poisoned = true;
            return SqlResult::failure(std::move(error));
        }
        return SqlResult::success(std::move(response));
    }
    std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> resultGuard(result,
                                                                         mysql_free_result);
    const auto columns = mysql_num_fields(result);
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        const auto lengths = mysql_fetch_lengths(result);
        SqlRow output;
        output.reserve(columns);
        for (unsigned int column = 0; column < columns; ++column) {
            if (!row[column]) output.emplace_back(std::nullopt);
            else output.emplace_back(std::string(row[column], lengths[column]));
        }
        response.rows.push_back(std::move(output));
    }
    if (mysql_errno(impl_->connection) != 0) {
        auto error = connectionError(impl_->connection);
        if (connectionLost(error)) impl_->poisoned = true;
        return SqlResult::failure(std::move(error));
    }
    return SqlResult::success(std::move(response));
}

SqlResult MySqlConnection::executePrepared(
    std::string_view sql, const std::vector<SqlValue>& parameters) {
    if (!impl_ || !impl_->connection || impl_->poisoned || !ensureThreadContext()) {
        return SqlResult::failure(
            {SqlErrorCode::Connection, 0, "mysql connection is not reusable"});
    }
    MYSQL_STMT* rawStatement = mysql_stmt_init(impl_->connection);
    if (!rawStatement) return SqlResult::failure(connectionError(impl_->connection));
    StatementGuard statement(rawStatement);
    if (mysql_stmt_prepare(rawStatement, sql.data(),
                           static_cast<unsigned long>(sql.size())) != 0) {
        auto error = statementError(rawStatement);
        if (connectionLost(error)) impl_->poisoned = true;
        return SqlResult::failure(std::move(error));
    }
    if (mysql_stmt_param_count(rawStatement) != parameters.size()) {
        return SqlResult::failure(
            {SqlErrorCode::InvalidData, 0, "prepared statement parameter count mismatch"});
    }

    std::vector<MYSQL_BIND> binds(parameters.size());
    std::vector<long long> signedValues(parameters.size());
    std::vector<unsigned long long> unsignedValues(parameters.size());
    std::vector<unsigned long> lengths(parameters.size());
    std::unique_ptr<bool[]> nulls(new bool[std::max<std::size_t>(1, parameters.size())]{});
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        auto& bind = binds[index];
        std::memset(&bind, 0, sizeof(bind));
        bind.length = &lengths[index];
        bind.is_null = &nulls[index];
        if (std::holds_alternative<std::nullptr_t>(parameters[index])) {
            nulls[index] = true;
            bind.buffer_type = MYSQL_TYPE_NULL;
        } else if (const auto* value = std::get_if<std::int64_t>(&parameters[index])) {
            signedValues[index] = *value;
            bind.buffer_type = MYSQL_TYPE_LONGLONG;
            bind.buffer = &signedValues[index];
        } else if (const auto* value = std::get_if<std::uint64_t>(&parameters[index])) {
            unsignedValues[index] = *value;
            bind.buffer_type = MYSQL_TYPE_LONGLONG;
            bind.buffer = &unsignedValues[index];
            bind.is_unsigned = true;
        } else if (const auto* value = std::get_if<std::string>(&parameters[index])) {
            lengths[index] = static_cast<unsigned long>(value->size());
            bind.buffer_type = MYSQL_TYPE_STRING;
            bind.buffer = const_cast<char*>(value->data());
            bind.buffer_length = lengths[index];
        } else {
            const auto& binaryValue =
                std::get<std::vector<unsigned char>>(parameters[index]);
            lengths[index] = 0;
            bind.buffer_type = MYSQL_TYPE_BLOB;
            bind.buffer = nullptr;
            bind.buffer_length = 0;
        }
    }
    if (!binds.empty() && mysql_stmt_bind_param(rawStatement, binds.data()) != 0) {
        return SqlResult::failure(statementError(rawStatement));
    }
    constexpr std::size_t maxChunk =
        static_cast<std::size_t>((std::numeric_limits<unsigned long>::max)());
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto* binary =
            std::get_if<std::vector<unsigned char>>(&parameters[index]);
        if (!binary) continue;
        std::size_t offset = 0;
        while (offset < binary->size()) {
            const auto chunkSize = (std::min)(maxChunk, binary->size() - offset);
            if (mysql_stmt_send_long_data(
                    rawStatement, static_cast<unsigned int>(index),
                    reinterpret_cast<const char*>(binary->data() + offset),
                    static_cast<unsigned long>(chunkSize)) != 0) {
                auto error = statementError(rawStatement);
                if (connectionLost(error)) impl_->poisoned = true;
                return SqlResult::failure(std::move(error));
            }
            offset += chunkSize;
        }
    }
    bool updateMaxLength = true;
    mysql_stmt_attr_set(rawStatement, STMT_ATTR_UPDATE_MAX_LENGTH, &updateMaxLength);
    if (mysql_stmt_execute(rawStatement) != 0) {
        auto error = statementError(rawStatement);
        if (connectionLost(error)) impl_->poisoned = true;
        return SqlResult::failure(std::move(error));
    }

    SqlResponse response;
    response.affectedRows = mysql_stmt_affected_rows(rawStatement);
    response.lastInsertId = mysql_stmt_insert_id(rawStatement);
    MYSQL_RES* metadata = mysql_stmt_result_metadata(rawStatement);
    if (!metadata) return SqlResult::success(std::move(response));
    std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> metadataGuard(metadata,
                                                                           mysql_free_result);
    if (mysql_stmt_store_result(rawStatement) != 0) {
        return SqlResult::failure(statementError(rawStatement));
    }
    const auto columns = mysql_num_fields(metadata);
    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    std::vector<MYSQL_BIND> outputs(columns);
    std::vector<std::vector<char>> buffers(columns);
    std::vector<unsigned long> outputLengths(columns);
    std::unique_ptr<bool[]> outputNulls(new bool[std::max<unsigned int>(1, columns)]{});
    std::unique_ptr<bool[]> outputErrors(new bool[std::max<unsigned int>(1, columns)]{});
    constexpr unsigned long maxColumnBytes = 16UL * 1024UL * 1024UL;
    for (unsigned int column = 0; column < columns; ++column) {
        if (fields[column].max_length > maxColumnBytes) {
            return SqlResult::failure(
                {SqlErrorCode::InvalidData, 0, "query result column exceeds safety limit"});
        }
        buffers[column].resize(std::max<unsigned long>(1, fields[column].max_length + 1));
        std::memset(&outputs[column], 0, sizeof(MYSQL_BIND));
        outputs[column].buffer_type = MYSQL_TYPE_STRING;
        outputs[column].buffer = buffers[column].data();
        outputs[column].buffer_length = static_cast<unsigned long>(buffers[column].size());
        outputs[column].length = &outputLengths[column];
        outputs[column].is_null = &outputNulls[column];
        outputs[column].error = &outputErrors[column];
    }
    if (columns > 0 && mysql_stmt_bind_result(rawStatement, outputs.data()) != 0) {
        return SqlResult::failure(statementError(rawStatement));
    }
    while (true) {
        const int fetched = mysql_stmt_fetch(rawStatement);
        if (fetched == MYSQL_NO_DATA) break;
        if (fetched == 1 || fetched == MYSQL_DATA_TRUNCATED) {
            return SqlResult::failure(statementError(rawStatement));
        }
        SqlRow row;
        row.reserve(columns);
        for (unsigned int column = 0; column < columns; ++column) {
            if (outputNulls[column]) row.emplace_back(std::nullopt);
            else row.emplace_back(
                std::string(buffers[column].data(), outputLengths[column]));
        }
        response.rows.push_back(std::move(row));
    }
    return SqlResult::success(std::move(response));
}

bool MySqlConnection::reusable() const noexcept {
    return impl_ && impl_->connection && !impl_->poisoned;
}

void MySqlConnection::poison() noexcept {
    if (impl_) impl_->poisoned = true;
}

}  // namespace warehouse::infrastructure::mysql
