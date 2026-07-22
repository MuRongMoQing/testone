#include "infrastructure/mysql/client_library.hpp"

#ifdef _WIN32
#include <mysql.h>
#else
#include <mysql/mysql.h>
#endif

namespace warehouse::infrastructure::mysql {
namespace {

thread_local std::unique_ptr<ThreadContext> currentThreadContext;

}  // namespace

application::Result<std::unique_ptr<ClientLibrary>, SqlError> ClientLibrary::create() {
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        return application::Result<std::unique_ptr<ClientLibrary>, SqlError>::failure(
            {SqlErrorCode::Connection, 0, "mysql client library initialization failed"});
    }
    return application::Result<std::unique_ptr<ClientLibrary>, SqlError>::success(
        std::unique_ptr<ClientLibrary>(new ClientLibrary()));
}

ClientLibrary::~ClientLibrary() { mysql_library_end(); }

ThreadContext::ThreadContext() : initialized_(mysql_thread_init() == 0) {}
ThreadContext::~ThreadContext() {
    if (initialized_) mysql_thread_end();
}

bool ensureThreadContext() noexcept {
    if (!currentThreadContext) currentThreadContext = std::make_unique<ThreadContext>();
    return currentThreadContext->initialized();
}

void releaseThreadContext() noexcept { currentThreadContext.reset(); }

}  // namespace warehouse::infrastructure::mysql
