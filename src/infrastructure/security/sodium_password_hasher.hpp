#pragma once

#include "application/security/password_hasher.hpp"

namespace warehouse::infrastructure::security {

class SodiumPasswordHasher final : public application::security::PasswordHasher {
public:
    bool verify(std::string_view hash, std::string_view password) const override;
    bool needsRehash(std::string_view hash) const override;
    application::Result<std::string, std::string> hash(
        std::string_view password) const override;
    void clear(std::string& sensitive) const noexcept override;
};

}  // namespace warehouse::infrastructure::security
