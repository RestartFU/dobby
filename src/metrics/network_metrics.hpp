#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_set>

namespace dobby {

struct NetworkMetricsSnapshot {
    bool connected{};
    std::optional<int> pingMilliseconds;
    std::optional<double> observedTicksPerSecond;
    std::size_t loadedChunks{};
    std::size_t chunksPerSecond{};
    std::optional<std::uint64_t> outstandingSubChunkRequests;
};

class NetworkMetricsTracker {
public:
    void recordPing(int lastPingMilliseconds, int averagePingMilliseconds,
                    std::uint64_t nowMilliseconds);
    void recordServerTick(std::uint64_t tick, std::uint64_t nowMilliseconds);
    void recordChunk(std::uint64_t nowMilliseconds);
    void recordChunkLoaded(
            std::uintptr_t levelIdentity, std::int32_t chunkX,
            std::int32_t chunkZ);
    void recordChunkUnloaded(
            std::uintptr_t levelIdentity, std::int32_t chunkX,
            std::int32_t chunkZ);
    void recordSubChunkRequest(std::uint64_t count);
    void recordSubChunkResponse(std::uint64_t count);
    NetworkMetricsSnapshot snapshot(std::uint64_t nowMilliseconds) const;
    void reset();
    void resetServerTicks();
    std::size_t retainedTickSamples() const;

private:
    struct TickSample {
        std::uint64_t tick{};
        std::uint64_t capturedAtMilliseconds{};
    };

    struct LoadedChunkIdentity {
        std::uintptr_t level{};
        std::int32_t x{};
        std::int32_t z{};

        bool operator==(const LoadedChunkIdentity&) const = default;
    };

    struct LoadedChunkIdentityHash {
        std::size_t operator()(const LoadedChunkIdentity& identity) const;
    };

    void pruneTickSamples(std::uint64_t nowMilliseconds);

    std::optional<int> lastPingMilliseconds_;
    std::optional<int> averagePingMilliseconds_;
    std::uint64_t pingCapturedAtMilliseconds_{};
    std::deque<TickSample> tickSamples_;
    std::unordered_set<LoadedChunkIdentity, LoadedChunkIdentityHash>
            loadedChunks_;
    std::deque<std::uint64_t> recentChunkMilliseconds_;
    std::uint64_t outstandingSubChunkRequests_{};
};

void recordNativePing(const void* peerIdentity, int lastPingMilliseconds,
                      int averagePingMilliseconds);
void recordObservedServerTick(const void* levelIdentity, std::uint64_t tick);
void recordLevelChunkDecode();
void recordClientChunkLoaded(
        const void* levelIdentity, std::int32_t chunkX, std::int32_t chunkZ);
void recordClientChunkUnloaded(
        const void* levelIdentity, std::int32_t chunkX, std::int32_t chunkZ);
void recordSubChunkRequest(std::uint64_t count);
void recordSubChunkResponse(std::uint64_t count);
void setOutstandingChunkMetricsAvailable(bool available);
NetworkMetricsSnapshot currentNetworkMetrics();
void resetNetworkMetrics();

} // namespace dobby
