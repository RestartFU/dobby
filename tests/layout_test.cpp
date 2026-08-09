#include "violation_layout.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

using packet_debugger::decodeViolation;

template <class T>
void store(std::byte* destination, T value) {
    std::memcpy(destination, &value, sizeof(value));
}
int main() {
    std::array<std::byte, 0x60> packet{};
    store<std::int32_t>(packet.data() + 0x30, 0);
    store<std::int32_t>(packet.data() + 0x34, 2);
    store<std::int32_t>(packet.data() + 0x38, 58);

    const std::string shortContext = "bad block palette";
    packet[0x40] = static_cast<std::byte>(shortContext.size() << 1U);
    std::memcpy(packet.data() + 0x41, shortContext.data(), shortContext.size());
    auto shortRecord = decodeViolation(packet.data());
    assert(shortRecord);
    assert(shortRecord->type == 0);
    assert(shortRecord->severity == 2);
    assert(shortRecord->packetId == 58);
    assert(shortRecord->context == shortContext);

    const std::string longContext(80, 'x');
    packet[0x40] = std::byte{1};
    store<std::size_t>(packet.data() + 0x48, longContext.size());
    store<const char*>(packet.data() + 0x50, longContext.data());
    auto longRecord = decodeViolation(packet.data());
    assert(longRecord);
    assert(longRecord->context == longContext);

    std::cout << "layout decoder tests passed\n";
}
