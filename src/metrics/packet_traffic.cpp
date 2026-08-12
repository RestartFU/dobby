#include "metrics/packet_traffic.hpp"

#include <chrono>
#include <mutex>

namespace dobby {
namespace {

std::uint64_t monotonicMilliseconds() {
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
}

std::mutex globalPacketTrafficMutex;
PacketTrafficTracker globalPacketTraffic;

} // namespace

void PacketTrafficTracker::recordPacket(
        PacketDirection direction, std::uint32_t bytes,
        std::uint64_t nowMilliseconds) {
    auto& packetTotal = direction == PacketDirection::incoming
            ? incomingPackets_ : outgoingPackets_;
    auto& byteTotal = direction == PacketDirection::incoming
            ? incomingBytes_ : outgoingBytes_;
    ++packetTotal;
    byteTotal += bytes;

    const std::uint64_t bucketStart =
            nowMilliseconds - nowMilliseconds % kRateBucketMilliseconds;
    auto& bucket = rateBuckets_[static_cast<std::size_t>(
            bucketStart / kRateBucketMilliseconds % kRateBucketCount)];
    if (!bucket.occupied || bucket.startMilliseconds != bucketStart) {
        bucket = {};
        bucket.startMilliseconds = bucketStart;
        bucket.occupied = true;
    }
    if (direction == PacketDirection::incoming) {
        ++bucket.incomingPackets;
        bucket.incomingBytes += bytes;
    } else {
        ++bucket.outgoingPackets;
        bucket.outgoingBytes += bytes;
    }
}

PacketTrafficSnapshot PacketTrafficTracker::snapshot(
        std::uint64_t nowMilliseconds) const {
    PacketTrafficSnapshot result;
    result.capturedAtMilliseconds = nowMilliseconds;
    result.incomingPackets = incomingPackets_;
    result.outgoingPackets = outgoingPackets_;
    result.incomingBytes = incomingBytes_;
    result.outgoingBytes = outgoingBytes_;

    constexpr std::uint64_t windowMilliseconds =
            kRateBucketCount * kRateBucketMilliseconds;
    for (const auto& bucket : rateBuckets_) {
        if (!bucket.occupied || bucket.startMilliseconds > nowMilliseconds ||
            nowMilliseconds - bucket.startMilliseconds >= windowMilliseconds) {
            continue;
        }
        result.incomingPacketsPerSecond += bucket.incomingPackets;
        result.outgoingPacketsPerSecond += bucket.outgoingPackets;
        result.incomingBytesPerSecond += bucket.incomingBytes;
        result.outgoingBytesPerSecond += bucket.outgoingBytes;
    }
    return result;
}

void PacketTrafficTracker::reset() {
    incomingPackets_ = 0;
    outgoingPackets_ = 0;
    incomingBytes_ = 0;
    outgoingBytes_ = 0;
    rateBuckets_ = {};
}

void recordPacketTraffic(PacketDirection direction, std::uint32_t bytes) {
    std::lock_guard lock(globalPacketTrafficMutex);
    globalPacketTraffic.recordPacket(
            direction, bytes, monotonicMilliseconds());
}

PacketTrafficSnapshot currentPacketTraffic() {
    std::lock_guard lock(globalPacketTrafficMutex);
    return globalPacketTraffic.snapshot(monotonicMilliseconds());
}

void resetPacketTraffic() {
    std::lock_guard lock(globalPacketTrafficMutex);
    globalPacketTraffic.reset();
}

} // namespace dobby
