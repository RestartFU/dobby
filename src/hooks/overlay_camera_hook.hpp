#pragma once

#include <cstdint>

namespace dobby {

inline constexpr std::uint64_t kWorldRenderFreshnessMilliseconds = 1000;

constexpr bool worldRenderIsFresh(
        std::uint64_t lastRenderMilliseconds,
        std::uint64_t nowMilliseconds) {
    return lastRenderMilliseconds != 0 &&
            lastRenderMilliseconds <= nowMilliseconds &&
            nowMilliseconds - lastRenderMilliseconds <=
                    kWorldRenderFreshnessMilliseconds;
}

void installOverlayCameraHook();
bool overlayCameraHookInstalled();
bool clientWorldRecentlyRendered();

} // namespace dobby
