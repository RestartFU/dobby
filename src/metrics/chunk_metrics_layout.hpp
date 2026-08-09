#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dobby {

std::optional<std::uint64_t> boundedVectorElementCount(
        std::uintptr_t begin, std::uintptr_t end, std::size_t elementSize,
        std::uint64_t maximumElements);

} // namespace dobby
