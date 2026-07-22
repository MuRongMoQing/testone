#include "infrastructure/mysql/connection_pool.hpp"

#include <cassert>
#include <chrono>
#include <memory>

namespace mysql = warehouse::infrastructure::mysql;

class FakeExecutor final : public mysql::SqlExecutor {
public:
    mysql::SqlResult executeTrusted(std::string_view) override {
        return mysql::SqlResult::success({});
    }
    mysql::SqlResult executePrepared(
        std::string_view, const std::vector<mysql::SqlValue>&) override {
        return mysql::SqlResult::success({});
    }
    bool reusable() const noexcept override { return !poisoned; }
    void poison() noexcept override { poisoned = true; }
    bool poisoned = false;
};

int main() {
    int created = 0;
    mysql::ConnectionPool pool(1, [&] {
        ++created;
        std::unique_ptr<mysql::SqlExecutor> executor = std::make_unique<FakeExecutor>();
        return warehouse::application::Result<std::unique_ptr<mysql::SqlExecutor>,
                                              mysql::SqlError>::success(std::move(executor));
    });

    {
        auto first = pool.acquire(std::chrono::milliseconds(1));
        assert(first);
        auto timedOut = pool.acquire(std::chrono::milliseconds(1));
        assert(!timedOut);
        assert(timedOut.error().code == mysql::SqlErrorCode::PoolExhausted);
    }
    assert(pool.available() == 1);
    {
        auto reused = pool.acquire(std::chrono::milliseconds(1));
        assert(reused);
        assert(created == 1);
        reused.value().executor().poison();
    }
    assert(pool.size() == 0);
    {
        auto replacement = pool.acquire(std::chrono::milliseconds(1));
        assert(replacement);
        assert(created == 2);
    }
}
