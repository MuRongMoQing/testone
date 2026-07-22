#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace warehouse::application::legacy {

struct LegacyPrincipal {
    std::string username;
    std::string role;
};

class LegacySessionStore {
public:
    virtual ~LegacySessionStore() = default;
    virtual std::string create(const LegacyPrincipal& principal) = 0;
    virtual std::optional<LegacyPrincipal> find(std::string_view token) const = 0;
};

}  // namespace warehouse::application::legacy
