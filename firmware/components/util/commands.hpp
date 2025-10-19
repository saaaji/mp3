#pragma once

#include <cstdint>

namespace bt {

enum class Command : std::uint8_t {
    kStartDiscovery
};

} // namespace bt

namespace wifi {

enum class Command : std::uint8_t {
    kSpinUp,
    kSpinDown
};

} // namespace wifi