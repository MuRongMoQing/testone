#pragma once

#include "application/legacy/legacy_session_store.hpp"

#include <map>
#include <mutex>

namespace warehouse::infrastructure::session {

class InMemoryLegacySessionStore final : public application::legacy::LegacySessionStore {
public:
    InMemoryLegacySessionStore();

    std::string create(const application::legacy::LegacyPrincipal& principal) override;
    std::optional<application::legacy::LegacyPrincipal> find(
        std::string_view token) const override;

private:
    mutable std::mutex mutex_;
    std::map<std::string, application::legacy::LegacyPrincipal> sessions_;
};

}  // namespace warehouse::infrastructure::session
