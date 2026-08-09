#include "metrics/network_metrics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <mutex>

namespace dobby {
namespace {

constexpr std::uint64_t kPingFreshnessMilliseconds = 3000;
constexpr std::uint64_t kTickFreshnessMilliseconds = 1500;
constexpr std::uint64_t kTickWindowMilliseconds = 3000;
constexpr std::uint64_t kMinimumTickWindowMilliseconds = 1000;
constexpr std::uint64_t kMinimumTickSampleIntervalMilliseconds = 50;
constexpr std::size_t kMaximumTickSamples = 64;
constexpr int kMaximumAcceptedPingMilliseconds = 600000;
constexpr std::uint64_t kMaximumAcceptedTickDelta = 1000000;
constexpr std::uint64_t kChunkRateWindowMilliseconds = 1000;
constexpr std::size_t kMaximumRecentChunkSamples = 4096;
constexpr std::size_t kMaximumLoadedChunks = 16384;

std::uint64_t monotonicMilliseconds() {
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
}

bool fresh(std::uint64_t capturedAt, std::uint64_t now, std::uint64_t limit) {
    return capturedAt <= now && now - capturedAt <= limit;
}

std::mutex globalMetricsMutex;
NetworkMetricsTracker globalMetrics;
const void* sampledLevelIdentity = nullptr;
const void* sampledPeerIdentity = nullptr;
std::atomic_bool outstandingChunkMetricsAvailable{false};

} // namespace

void NetworkMetricsTracker::recordPing(
        int lastPingMilliseconds, int averagePingMilliseconds,
        std::uint64_t nowMilliseconds) {
    const auto validPing = [](int value) {
        return value >= 0 && value <= kMaximumAcceptedPingMilliseconds;
    };
    if (!validPing(lastPingMilliseconds) && !validPing(averagePingMilliseconds))
        return;
    lastPingMilliseconds_ = validPing(lastPingMilliseconds)
            ? std::optional<int>(lastPingMilliseconds) : std::nullopt;
    averagePingMilliseconds_ = validPing(averagePingMilliseconds)
            ? std::optional<int>(averagePingMilliseconds) : std::nullopt;
    pingCapturedAtMilliseconds_ = nowMilliseconds;
}

void NetworkMetricsTracker::recordServerTick(
        std::uint64_t tick, std::uint64_t nowMilliseconds) {
    if (!tickSamples_.empty()) {
        const auto& last = tickSamples_.back();
        if (nowMilliseconds < last.capturedAtMilliseconds || tick < last.tick) {
            tickSamples_.clear();
        } else if (nowMilliseconds - last.capturedAtMilliseconds <
                   kMinimumTickSampleIntervalMilliseconds) {
            return;
        }
    }
    tickSamples_.push_back({tick, nowMilliseconds});
    pruneTickSamples(nowMilliseconds);
}

void NetworkMetricsTracker::recordChunk(std::uint64_t nowMilliseconds) {
    recentChunkMilliseconds_.push_back(nowMilliseconds);
    while (!recentChunkMilliseconds_.empty() &&
           recentChunkMilliseconds_.front() <= nowMilliseconds &&
           nowMilliseconds - recentChunkMilliseconds_.front() >=
                   kChunkRateWindowMilliseconds) {
        recentChunkMilliseconds_.pop_front();
    }
    while (recentChunkMilliseconds_.size() > kMaximumRecentChunkSamples)
        recentChunkMilliseconds_.pop_front();
}

std::size_t NetworkMetricsTracker::LoadedChunkIdentityHash::operator()(
        const LoadedChunkIdentity& identity) const {
    std::size_t value = std::hash<std::uintptr_t>{}(identity.level);
    value ^= std::hash<std::int32_t>{}(identity.x) + 0x9e3779b9U +
            (value << 6U) + (value >> 2U);
    value ^= std::hash<std::int32_t>{}(identity.z) + 0x9e3779b9U +
            (value << 6U) + (value >> 2U);
    return value;
}

void NetworkMetricsTracker::recordChunkLoaded(
        std::uintptr_t levelIdentity, std::int32_t chunkX,
        std::int32_t chunkZ) {
    if (levelIdentity == 0)
        return;
    const LoadedChunkIdentity identity{levelIdentity, chunkX, chunkZ};
    if (loadedChunks_.size() >= kMaximumLoadedChunks &&
        !loadedChunks_.contains(identity)) {
        return;
    }
    loadedChunks_.insert(identity);
}

void NetworkMetricsTracker::recordChunkUnloaded(
        std::uintptr_t levelIdentity, std::int32_t chunkX,
        std::int32_t chunkZ) {
    if (levelIdentity == 0)
        return;
    loadedChunks_.erase({levelIdentity, chunkX, chunkZ});
}

void NetworkMetricsTracker::recordSubChunkRequest(std::uint64_t count) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    outstandingSubChunkRequests_ = count > maximum - outstandingSubChunkRequests_
            ? maximum : outstandingSubChunkRequests_ + count;
}

void NetworkMetricsTracker::recordSubChunkResponse(std::uint64_t count) {
    outstandingSubChunkRequests_ = count >= outstandingSubChunkRequests_
            ? 0 : outstandingSubChunkRequests_ - count;
}

NetworkMetricsSnapshot NetworkMetricsTracker::snapshot(
        std::uint64_t nowMilliseconds) const {
    NetworkMetricsSnapshot result;
    if ((lastPingMilliseconds_ || averagePingMilliseconds_) &&
        fresh(pingCapturedAtMilliseconds_, nowMilliseconds,
              kPingFreshnessMilliseconds)) {
        result.connected = true;
        result.pingMilliseconds = averagePingMilliseconds_
                ? averagePingMilliseconds_ : lastPingMilliseconds_;
    }
    if (!result.connected || tickSamples_.size() < 2) {
        if (result.connected) {
            result.loadedChunks = loadedChunks_.size();
            result.chunksPerSecond = static_cast<std::size_t>(std::count_if(
                    recentChunkMilliseconds_.begin(), recentChunkMilliseconds_.end(),
                    [nowMilliseconds](std::uint64_t capturedAt) {
                        return capturedAt <= nowMilliseconds &&
                                nowMilliseconds - capturedAt <
                                        kChunkRateWindowMilliseconds;
                    }));
            if (outstandingChunkMetricsAvailable.load(std::memory_order_relaxed))
                result.outstandingSubChunkRequests = outstandingSubChunkRequests_;
        }
        return result;
    }

    result.loadedChunks = loadedChunks_.size();
    result.chunksPerSecond = static_cast<std::size_t>(std::count_if(
            recentChunkMilliseconds_.begin(), recentChunkMilliseconds_.end(),
            [nowMilliseconds](std::uint64_t capturedAt) {
                return capturedAt <= nowMilliseconds &&
                        nowMilliseconds - capturedAt <
                                kChunkRateWindowMilliseconds;
            }));
    if (outstandingChunkMetricsAvailable.load(std::memory_order_relaxed))
        result.outstandingSubChunkRequests = outstandingSubChunkRequests_;

    const auto& first = tickSamples_.front();
    const auto& last = tickSamples_.back();
    if (!fresh(last.capturedAtMilliseconds, nowMilliseconds,
               kTickFreshnessMilliseconds) ||
        last.capturedAtMilliseconds <= first.capturedAtMilliseconds ||
        last.capturedAtMilliseconds - first.capturedAtMilliseconds <
                kMinimumTickWindowMilliseconds ||
        last.tick < first.tick || last.tick - first.tick > kMaximumAcceptedTickDelta) {
        return result;
    }
    const double elapsedSeconds = static_cast<double>(
            last.capturedAtMilliseconds - first.capturedAtMilliseconds) / 1000.0;
    result.observedTicksPerSecond =
            static_cast<double>(last.tick - first.tick) / elapsedSeconds;
    return result;
}

void NetworkMetricsTracker::reset() {
    lastPingMilliseconds_.reset();
    averagePingMilliseconds_.reset();
    pingCapturedAtMilliseconds_ = 0;
    tickSamples_.clear();
    loadedChunks_.clear();
    recentChunkMilliseconds_.clear();
    outstandingSubChunkRequests_ = 0;
}

void NetworkMetricsTracker::resetServerTicks() {
    tickSamples_.clear();
}

std::size_t NetworkMetricsTracker::retainedTickSamples() const {
    return tickSamples_.size();
}

void NetworkMetricsTracker::pruneTickSamples(std::uint64_t nowMilliseconds) {
    while (tickSamples_.size() > 1 &&
           tickSamples_.front().capturedAtMilliseconds <= nowMilliseconds &&
           nowMilliseconds - tickSamples_.front().capturedAtMilliseconds >
                   kTickWindowMilliseconds) {
        tickSamples_.pop_front();
    }
    while (tickSamples_.size() > kMaximumTickSamples)
        tickSamples_.pop_front();
}

void recordNativePing(const void* peerIdentity, int lastPingMilliseconds,
                      int averagePingMilliseconds) {
    if (peerIdentity == nullptr)
        return;
    std::lock_guard lock(globalMetricsMutex);
    // Chunk packets can be decoded just before the first RakNet update calls
    // this function. Preserve those observations when establishing the first
    // peer identity; only a transition from an already-observed peer starts a
    // new metrics session.
    if (sampledPeerIdentity != nullptr && sampledPeerIdentity != peerIdentity) {
        globalMetrics.reset();
        sampledLevelIdentity = nullptr;
    }
    sampledPeerIdentity = peerIdentity;
    globalMetrics.recordPing(
            lastPingMilliseconds, averagePingMilliseconds,
            monotonicMilliseconds());
}

void recordObservedServerTick(const void* levelIdentity, std::uint64_t tick) {
    if (levelIdentity == nullptr)
        return;
    std::lock_guard lock(globalMetricsMutex);
    if (sampledLevelIdentity != levelIdentity) {
        globalMetrics.resetServerTicks();
        sampledLevelIdentity = levelIdentity;
    }
    globalMetrics.recordServerTick(tick, monotonicMilliseconds());
}

void recordLevelChunkDecode() {
    std::lock_guard lock(globalMetricsMutex);
    globalMetrics.recordChunk(monotonicMilliseconds());
}

void recordClientChunkLoaded(
        const void* levelIdentity, std::int32_t chunkX,
        std::int32_t chunkZ) {
    if (levelIdentity == nullptr)
        return;
    std::lock_guard lock(globalMetricsMutex);
    globalMetrics.recordChunkLoaded(
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunkX, chunkZ);
}

void recordClientChunkUnloaded(
        const void* levelIdentity, std::int32_t chunkX,
        std::int32_t chunkZ) {
    if (levelIdentity == nullptr)
        return;
    std::lock_guard lock(globalMetricsMutex);
    globalMetrics.recordChunkUnloaded(
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunkX, chunkZ);
}

void recordSubChunkRequest(std::uint64_t count) {
    std::lock_guard lock(globalMetricsMutex);
    globalMetrics.recordSubChunkRequest(count);
}

void recordSubChunkResponse(std::uint64_t count) {
    std::lock_guard lock(globalMetricsMutex);
    globalMetrics.recordSubChunkResponse(count);
}

void setOutstandingChunkMetricsAvailable(bool available) {
    outstandingChunkMetricsAvailable.store(available, std::memory_order_relaxed);
}

NetworkMetricsSnapshot currentNetworkMetrics() {
    std::lock_guard lock(globalMetricsMutex);
    return globalMetrics.snapshot(monotonicMilliseconds());
}

void resetNetworkMetrics() {
    std::lock_guard lock(globalMetricsMutex);
    globalMetrics.reset();
    sampledLevelIdentity = nullptr;
    sampledPeerIdentity = nullptr;
}

} // namespace dobby
