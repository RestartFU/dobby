#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dobby {

struct MinecraftImage {
    std::uintptr_t base{};
    std::uintptr_t executableBegin{};
    std::uintptr_t executableEnd{};
};

MinecraftImage findMinecraftImage();
bool addressIsExecutable(const MinecraftImage& image, std::uintptr_t address);

template <std::size_t Size>
bool matchesSignature(const void* address, const std::array<std::uint8_t, Size>& signature) {
    const auto* bytes = static_cast<const std::uint8_t*>(address);
    for (std::size_t index = 0; index < Size; ++index) {
        if (bytes[index] != signature[index])
            return false;
    }
    return true;
}

} // namespace dobby
