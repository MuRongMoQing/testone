#include "infrastructure/session/in_memory_legacy_session_store.hpp"

#include <sodium.h>

#include <array>
#include <stdexcept>

namespace warehouse::infrastructure::session {

InMemoryLegacySessionStore::InMemoryLegacySessionStore() {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed");
}

std::string InMemoryLegacySessionStore::create(
    const application::legacy::LegacyPrincipal& principal) {
    std::array<unsigned char, 32> random{};
    std::array<char, 65> encoded{};
    randombytes_buf(random.data(), random.size());
    sodium_bin2hex(encoded.data(), encoded.size(), random.data(), random.size());
    std::string token(encoded.data());
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[token] = principal;
    return token;
}

std::optional<application::legacy::LegacyPrincipal> InMemoryLegacySessionStore::find(
    std::string_view token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(std::string(token));
    if (found == sessions_.end()) return std::nullopt;
    return found->second;
}

}  // namespace warehouse::infrastructure::session
