#pragma once

#include <cstdint>
#include <string>

namespace warehouse::domain::legacy {

struct UserRecord {
    std::uint64_t id = 0;
    std::string username;
    std::string passwordHash;
    std::string role;
};

struct GoodsRecord {
    std::int64_t id = 0;
    std::string name;
    std::string location;
    std::string status;
    std::string storedAt;
    std::string takenAt;
    std::string operatorName;
};

struct GoodsFilter {
    std::string name;
    std::string status;
};

struct NewGoods {
    std::string name;
    std::string location;
    std::string operatorName;
};

enum class TakeGoodsOutcome {
    Taken,
    NotFound,
    AlreadyTaken,
};

}  // namespace warehouse::domain::legacy
