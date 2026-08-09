#pragma once

#include "diagnostics/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace dobby {

// Verified from the supported Android ARM64 ReadOnlyBinaryStream::read body:
// mView.data, mView.size, mReadPointer, and mHasOverflowed are read at these offsets.
inline constexpr std::ptrdiff_t kStreamViewDataOffset = 0x20;
inline constexpr std::ptrdiff_t kStreamViewSizeOffset = 0x28;
inline constexpr std::ptrdiff_t kStreamReadPointerOffset = 0x30;
inline constexpr std::ptrdiff_t kStreamOverflowOffset = 0x38;

struct StreamView {
    const std::uint8_t* data{};
    std::size_t size{};
    std::size_t readPointer{};
    bool overflowed{};
};

std::optional<StreamView> inspectStream(const void* stream);
void captureStreamReadAttempt(const void* stream, std::size_t requested, std::size_t rawCaptureLimit);
std::optional<StreamFailure> recentStreamFailure(std::chrono::milliseconds maximumAge);
void clearStreamProbe();

} // namespace dobby
