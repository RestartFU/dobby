#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace packet_debugger {

constexpr std::ptrdiff_t kViolationTypeOffset = 0x30;
constexpr std::ptrdiff_t kViolationSeverityOffset = 0x34;
constexpr std::ptrdiff_t kPacketIdOffset = 0x38;
constexpr std::ptrdiff_t kContextOffset = 0x40;
constexpr std::size_t kMaxContextLength = 1024 * 1024;

struct ViolationRecord {
    std::int32_t type{};
    std::int32_t severity{};
    std::int32_t packetId{};
    std::string context;
};

template <class T>
T readUnaligned(const std::byte* address) {
    T value{};
    std::memcpy(&value, address, sizeof(value));
    return value;
}

inline std::optional<std::string_view> readAndroidLibcxxString(const std::byte* object) {
    const auto tag = readUnaligned<std::uint8_t>(object);
    const bool isLong = (tag & 1U) != 0U;
    if (!isLong) {
        const auto length = static_cast<std::size_t>(tag >> 1U);
        return std::string_view(reinterpret_cast<const char*>(object + 1), length);
    }

    const auto length = readUnaligned<std::size_t>(object + 8);
    const auto data = readUnaligned<const char*>(object + 16);
    if (data == nullptr || length > kMaxContextLength)
        return std::nullopt;
    return std::string_view(data, length);
}

inline std::optional<ViolationRecord> decodeViolation(const void* packet) {
    if (packet == nullptr)
        return std::nullopt;

    const auto* bytes = static_cast<const std::byte*>(packet);
    const auto context = readAndroidLibcxxString(bytes + kContextOffset);
    if (!context)
        return std::nullopt;

    ViolationRecord result;
    result.type = readUnaligned<std::int32_t>(bytes + kViolationTypeOffset);
    result.severity = readUnaligned<std::int32_t>(bytes + kViolationSeverityOffset);
    result.packetId = readUnaligned<std::int32_t>(bytes + kPacketIdOffset);
    result.context.assign(context->data(), context->size());
    return result;
}

} // namespace packet_debugger
