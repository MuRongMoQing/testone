#pragma once

#include <string>

namespace warehouse::security {

enum class StoredPasswordKind {
    LegacyPlaintext,
    PasswordHash,
    InvalidPasswordHash,
};

bool initializePasswordHashing(std::string& error);
bool hashPassword(const std::string& password, std::string& hash, std::string& error);
bool verifyPassword(const std::string& hash, const std::string& password);
bool passwordNeedsRehash(const std::string& hash);
StoredPasswordKind classifyStoredPassword(const std::string& value);
void clearSensitiveString(std::string& value);

}  // namespace warehouse::security
