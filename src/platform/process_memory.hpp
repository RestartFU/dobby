#pragma once

#include <cstdint>
#include <optional>

namespace dobby {

// Returns this Minecraft client process's resident set size. Virtual address
// space is deliberately excluded because it is not memory currently in use.
std::optional<std::uint64_t> currentProcessResidentBytes();

} // namespace dobby
