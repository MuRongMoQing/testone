#pragma once

#include "application/common/result.hpp"

#include <string>
#include <string_view>

namespace warehouse::application::security {

class PasswordHasher {
public:
    virtual ~PasswordHasher() = default;
    virtual bool verify(std::string_view hash, std::string_view password) const = 0;
    virtual bool needsRehash(std::string_view hash) const = 0;
    virtual Result<std::string, std::string> hash(std::string_view password) const = 0;
    virtual void clear(std::string& sensitive) const noexcept = 0;
};

}  // namespace warehouse::application::security
