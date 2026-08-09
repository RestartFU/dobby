#include "metrics/chunk_metrics_layout.hpp"

namespace dobby {

std::optional<std::uint64_t> boundedVectorElementCount(
        std::uintptr_t begin, std::uintptr_t end, std::size_t elementSize,
        std::uint64_t maximumElements) {
    if (elementSize == 0)
        return std::nullopt;
    if (begin == 0 || end == 0)
        return begin == 0 && end == 0
                ? std::optional<std::uint64_t>(0) : std::nullopt;
    if (end < begin)
        return std::nullopt;
    const std::uintptr_t bytes = end - begin;
    if (bytes % elementSize != 0)
        return std::nullopt;
    const auto count = static_cast<std::uint64_t>(bytes / elementSize);
    return count <= maximumElements
            ? std::optional<std::uint64_t>(count) : std::nullopt;
}

} // namespace dobby
