#include "infrastructure/mysql/connection_pool.hpp"

#include <stdexcept>
#include <utility>

namespace warehouse::infrastructure::mysql {

ConnectionLease::ConnectionLease(ConnectionPool& pool,
                                 std::unique_ptr<SqlExecutor> executor) noexcept
    : pool_(&pool), executor_(std::move(executor)) {}

ConnectionLease::ConnectionLease(ConnectionLease&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)), executor_(std::move(other.executor_)) {}

ConnectionLease& ConnectionLease::operator=(ConnectionLease&& other) noexcept {
    if (this != &other) {
        release();
        pool_ = std::exchange(other.pool_, nullptr);
        executor_ = std::move(other.executor_);
    }
    return *this;
}

ConnectionLease::~ConnectionLease() { release(); }

SqlExecutor& ConnectionLease::executor() const {
    if (!executor_) throw std::logic_error("connection lease is empty");
    return *executor_;
}

void ConnectionLease::release() noexcept {
    if (pool_ && executor_) pool_->giveBack(std::move(executor_));
    pool_ = nullptr;
}

ConnectionPool::ConnectionPool(std::size_t capacity, ExecutorFactory factory)
    : capacity_(capacity), factory_(std::move(factory)) {
    if (capacity_ == 0 || !factory_) {
        throw std::invalid_argument("connection pool requires a positive capacity and factory");
    }
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    available_.clear();
    total_ = 0;
    changed_.notify_all();
}

application::Result<ConnectionLease, SqlError> ConnectionPool::acquire(
    std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto ready = [this] {
        return stopping_ || !available_.empty() || total_ < capacity_;
    };
    if (!changed_.wait_for(lock, timeout, ready) || stopping_) {
        return application::Result<ConnectionLease, SqlError>::failure(
            {SqlErrorCode::PoolExhausted, 0, "database connection pool wait timed out"});
    }
    if (!available_.empty()) {
        auto executor = std::move(available_.back());
        available_.pop_back();
        return application::Result<ConnectionLease, SqlError>::success(
            ConnectionLease(*this, std::move(executor)));
    }
    ++total_;
    lock.unlock();
    auto created = factory_();
    if (!created) {
        lock.lock();
        --total_;
        lock.unlock();
        changed_.notify_one();
        return application::Result<ConnectionLease, SqlError>::failure(created.error());
    }
    return application::Result<ConnectionLease, SqlError>::success(
        ConnectionLease(*this, std::move(created.value())));
}

std::size_t ConnectionPool::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_;
}

std::size_t ConnectionPool::available() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_.size();
}

void ConnectionPool::giveBack(std::unique_ptr<SqlExecutor> executor) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || !executor || !executor->reusable()) {
        if (total_ > 0) --total_;
    } else {
        available_.push_back(std::move(executor));
    }
    changed_.notify_one();
}

}  // namespace warehouse::infrastructure::mysql
