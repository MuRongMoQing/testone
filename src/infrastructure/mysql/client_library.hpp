#pragma once

#include "application/common/result.hpp"
#include "infrastructure/mysql/executor.hpp"

#include <memory>

namespace warehouse::infrastructure::mysql {

class ClientLibrary final {
public:
    static application::Result<std::unique_ptr<ClientLibrary>, SqlError> create();
    ~ClientLibrary();

    ClientLibrary(const ClientLibrary&) = delete;
    ClientLibrary& operator=(const ClientLibrary&) = delete;

private:
    ClientLibrary() = default;
};

class ThreadContext final {
public:
    ThreadContext();
    ~ThreadContext();

    ThreadContext(const ThreadContext&) = delete;
    ThreadContext& operator=(const ThreadContext&) = delete;
    bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

bool ensureThreadContext() noexcept;
void releaseThreadContext() noexcept;

}  // namespace warehouse::infrastructure::mysql
