#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dobby {

enum class PacketDirection : std::uint8_t {
    incoming,
    outgoing,
};

struct PacketTrafficSnapshot {
    std::uint64_t capturedAtMilliseconds{};
    std::uint64_t incomingPackets{};
    std::uint64_t outgoingPackets{};
    std::uint64_t incomingBytes{};
    std::uint64_t outgoingBytes{};
    std::uint64_t incomingPacketsPerSecond{};
    std::uint64_t outgoingPacketsPerSecond{};
    std::uint64_t incomingBytesPerSecond{};
    std::uint64_t outgoingBytesPerSecond{};
};

class PacketTrafficTracker {
public:
    void recordPacket(PacketDirection direction, std::uint32_t bytes,
                      std::uint64_t nowMilliseconds);
    PacketTrafficSnapshot snapshot(std::uint64_t nowMilliseconds) const;
    void reset();

private:
    struct RateBucket {
        std::uint64_t startMilliseconds{};
        std::uint64_t incomingPackets{};
        std::uint64_t outgoingPackets{};
        std::uint64_t incomingBytes{};
        std::uint64_t outgoingBytes{};
        bool occupied{};
    };

    static constexpr std::size_t kRateBucketCount = 10;
    static constexpr std::uint64_t kRateBucketMilliseconds = 100;

    std::uint64_t incomingPackets_{};
    std::uint64_t outgoingPackets_{};
    std::uint64_t incomingBytes_{};
    std::uint64_t outgoingBytes_{};
    std::array<RateBucket, kRateBucketCount> rateBuckets_{};
};

void recordPacketTraffic(PacketDirection direction, std::uint32_t bytes);
PacketTrafficSnapshot currentPacketTraffic();
void resetPacketTraffic();

} // namespace dobby
