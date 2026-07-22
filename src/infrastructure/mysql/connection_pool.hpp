#pragma once

#include "application/common/result.hpp"
#include "infrastructure/mysql/executor.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace warehouse::infrastructure::mysql {

using ExecutorFactory = std::function<
    application::Result<std::unique_ptr<SqlExecutor>, SqlError>()>;

class ConnectionPool;

class ConnectionLease final {
public:
    ConnectionLease(ConnectionLease&& other) noexcept;
    ConnectionLease& operator=(ConnectionLease&& other) noexcept;
    ConnectionLease(const ConnectionLease&) = delete;
    ConnectionLease& operator=(const ConnectionLease&) = delete;
    ~ConnectionLease();

    SqlExecutor& executor() const;
    explicit operator bool() const noexcept { return executor_ != nullptr; }

private:
    friend class ConnectionPool;
    ConnectionLease(ConnectionPool& pool, std::unique_ptr<SqlExecutor> executor) noexcept;
    void release() noexcept;
    ConnectionPool* pool_ = nullptr;
    std::unique_ptr<SqlExecutor> executor_;
};

class ConnectionPool final {
public:
    ConnectionPool(std::size_t capacity, ExecutorFactory factory);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    application::Result<ConnectionLease, SqlError> acquire(
        std::chrono::milliseconds timeout);
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t size() const noexcept;
    std::size_t available() const noexcept;

private:
    friend class ConnectionLease;
    void giveBack(std::unique_ptr<SqlExecutor> executor) noexcept;

    const std::size_t capacity_;
    ExecutorFactory factory_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::unique_ptr<SqlExecutor>> available_;
    std::size_t total_ = 0;
    bool stopping_ = false;
};

}  // namespace warehouse::infrastructure::mysql
