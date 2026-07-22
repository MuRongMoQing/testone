#pragma once

#include <string>
#include <string_view>

namespace warehouse::infrastructure::mysql {

std::string sha256Hex(std::string_view input);

}  // namespace warehouse::infrastructure::mysql
