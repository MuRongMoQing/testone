#include "infrastructure/security/sodium_password_hasher.hpp"

#include "password_hash.hpp"

namespace warehouse::infrastructure::security {

bool SodiumPasswordHasher::verify(std::string_view hash, std::string_view password) const {
    const std::string ownedHash(hash);
    std::string ownedPassword(password);
    const bool verified = warehouse::security::verifyPassword(ownedHash, ownedPassword);
    warehouse::security::clearSensitiveString(ownedPassword);
    return verified;
}

bool SodiumPasswordHasher::needsRehash(std::string_view hash) const {
    return warehouse::security::passwordNeedsRehash(std::string(hash));
}

application::Result<std::string, std::string> SodiumPasswordHasher::hash(
    std::string_view password) const {
    std::string output;
    std::string failure;
    std::string ownedPassword(password);
    const bool hashed = warehouse::security::hashPassword(ownedPassword, output, failure);
    warehouse::security::clearSensitiveString(ownedPassword);
    if (!hashed) {
        return application::Result<std::string, std::string>::failure(std::move(failure));
    }
    return application::Result<std::string, std::string>::success(std::move(output));
}

void SodiumPasswordHasher::clear(std::string& sensitive) const noexcept {
    warehouse::security::clearSensitiveString(sensitive);
}

}  // namespace warehouse::infrastructure::security
