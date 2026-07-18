#include "password_hash.hpp"

#include <sodium.h>

#include <array>

namespace warehouse::security {
namespace {

constexpr auto kOpsLimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
constexpr auto kMemLimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;

bool looksLikeArgonHash(const std::string& value) {
    return value.rfind("$argon2id$", 0) == 0 || value.rfind("$argon2i$", 0) == 0;
}

}  // namespace

bool initializePasswordHashing(std::string& error) {
    if (sodium_init() < 0) {
        error = "libsodium initialization failed";
        return false;
    }
    return true;
}

bool hashPassword(const std::string& password, std::string& hash, std::string& error) {
    std::array<char, crypto_pwhash_STRBYTES> output{};
    if (crypto_pwhash_str(output.data(), password.data(),
                          static_cast<unsigned long long>(password.size()),
                          kOpsLimit, kMemLimit) != 0) {
        error = "password hashing failed";
        return false;
    }
    hash.assign(output.data());
    return true;
}

bool verifyPassword(const std::string& hash, const std::string& password) {
    return crypto_pwhash_str_verify(hash.c_str(), password.data(),
                                    static_cast<unsigned long long>(password.size())) == 0;
}

bool passwordNeedsRehash(const std::string& hash) {
    return crypto_pwhash_str_needs_rehash(hash.c_str(), kOpsLimit, kMemLimit) == 1;
}

StoredPasswordKind classifyStoredPassword(const std::string& value) {
    if (!looksLikeArgonHash(value)) {
        return StoredPasswordKind::LegacyPlaintext;
    }
    if (crypto_pwhash_str_needs_rehash(value.c_str(), kOpsLimit, kMemLimit) < 0) {
        return StoredPasswordKind::InvalidPasswordHash;
    }
    return StoredPasswordKind::PasswordHash;
}

void clearSensitiveString(std::string& value) {
    if (!value.empty()) {
        sodium_memzero(value.data(), value.size());
        value.clear();
    }
}

}  // namespace warehouse::security
