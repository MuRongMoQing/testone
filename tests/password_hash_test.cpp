#include "password_hash.hpp"

#include <iostream>
#include <string>

int main() {
    using warehouse::security::StoredPasswordKind;

    int failures = 0;
    const auto expect = [&failures](const bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    };

    std::string error;
    if (!warehouse::security::initializePasswordHashing(error)) {
        std::cerr << "FAILED: " << error << '\n';
        return 1;
    }
    expect(warehouse::security::classifyStoredPassword("legacy-password") ==
               StoredPasswordKind::LegacyPlaintext,
           "legacy plaintext classification");
    expect(warehouse::security::classifyStoredPassword("$argon2id$broken") ==
               StoredPasswordKind::InvalidPasswordHash,
           "invalid Argon2 hash classification");

    std::string firstHash;
    std::string secondHash;
    expect(warehouse::security::hashPassword(
               "correct horse battery staple", firstHash, error),
           "first Argon2id hash");
    expect(warehouse::security::hashPassword(
               "correct horse battery staple", secondHash, error),
           "second Argon2id hash");
    expect(firstHash != secondHash, "unique salts");
    expect(warehouse::security::classifyStoredPassword(firstHash) ==
               StoredPasswordKind::PasswordHash,
           "valid Argon2 hash classification");
    expect(warehouse::security::verifyPassword(
               firstHash, "correct horse battery staple"),
           "correct password verification");
    expect(!warehouse::security::verifyPassword(firstHash, "incorrect password"),
           "incorrect password rejection");
    expect(!warehouse::security::passwordNeedsRehash(firstHash),
           "current parameters do not need rehashing");

    std::string sensitive = "temporary secret";
    warehouse::security::clearSensitiveString(sensitive);
    expect(sensitive.empty(), "sensitive string clearing");

    return failures == 0 ? 0 : 1;
}
