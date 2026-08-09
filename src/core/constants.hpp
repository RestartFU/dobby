#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dobby {

inline constexpr char kDobbyVersion[] = "2.1.3";
inline constexpr char kMinecraftVersion[] = "1.26.40.5";
inline constexpr char kMinecraftBuildId[] = "5893edc8d56c93cbdb50e0f9436320236b78c89d";
inline constexpr char kAbi[] = "arm64-v8a";

inline constexpr std::size_t kDefaultHistoryLimit = 100;
inline constexpr std::size_t kMaximumHistoryLimit = 1000;
inline constexpr std::size_t kDefaultRawCaptureLimit = 2048;
inline constexpr std::size_t kMaximumRawCaptureLimit = 65536;

namespace target {

// All offsets and signatures are for the exact kMinecraftBuildId image.
inline constexpr std::uintptr_t kViolationGetIdOffset = 0x0cfa1cb4;
inline constexpr std::uintptr_t kViolationGetIdVtableSlotOffset = 0x120f7160;
inline constexpr std::array<std::uint8_t, 8> kViolationGetIdSignature{
        0x80, 0x13, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6};

inline constexpr std::uintptr_t kStreamReadOffset = 0x11a7b044;
inline constexpr std::uintptr_t kStreamReadVtableSlotOffset = 0x1249dac8;
inline constexpr std::array<std::uint8_t, 16> kStreamReadSignature{
        0xff, 0x83, 0x04, 0xd1, 0xfd, 0x7b, 0x0e, 0xa9,
        0xfc, 0x7b, 0x00, 0xf9, 0xf6, 0x57, 0x10, 0xa9};

inline constexpr std::uintptr_t kPacketEndCheckOffset = 0x11a7adb4;
inline constexpr std::array<std::uint8_t, 16> kPacketEndCheckSignature{
        0xff, 0x43, 0x02, 0xd1, 0xfd, 0x7b, 0x06, 0xa9,
        0xf5, 0x3b, 0x00, 0xf9, 0xf4, 0x4f, 0x08, 0xa9};

} // namespace target
} // namespace dobby
