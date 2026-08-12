#pragma once

#include "ui/chest_esp.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dobby {

enum class OreKind : std::uint8_t {
    coal,
    iron,
    copper,
    gold,
    redstone,
    lapis,
    diamond,
    emerald,
    quartz,
    ancientDebris,
    count,
};

struct OreBlockNameClassification {
    bool cacheable{};
    std::optional<OreKind> ore;

    bool operator==(const OreBlockNameClassification&) const = default;
};

struct OreBlock {
    BlockPosition position;
    OreKind kind{};

    bool operator==(const OreBlock&) const = default;
};

struct OreSubChunkSnapshot {
    std::int16_t absoluteSubChunk{};
    std::vector<OreBlock> ores;
};

struct OreEspObservation {
    EntityAabb bounds;
    OreKind kind{};
};

enum class ChunkOreUpdateResult {
    accepted,
    invalidInput,
    chunkCapacityReached,
    positionCapacityReached,
};

struct OreChunkScanTarget {
    const void* chunk{};
    const void* level{};
    ChunkPosition position;

    bool operator==(const OreChunkScanTarget&) const = default;
};

enum class OreChunkTrackResult {
    accepted,
    acceptedWithEviction,
    existingOwner,
    invalidInput,
    capacityReached,
};

// Maintains a bounded nearest-first scan queue. Native chunk lifetime is
// synchronized by the hook layer; this portable class owns no game memory.
class OreRescanSchedule {
public:
    OreRescanSchedule(
            std::size_t capacity,
            std::uint64_t periodicIntervalMicroseconds);

    OreChunkTrackResult track(
            OreChunkScanTarget target,
            std::optional<OreChunkScanTarget>* evictedTarget = nullptr);
    OreChunkTrackResult trackIfAbsent(
            OreChunkScanTarget target,
            std::optional<OreChunkScanTarget>* evictedTarget = nullptr);
    bool markDirty(OreChunkScanTarget target);
    bool remove(OreChunkScanTarget target);
    std::optional<OreChunkScanTarget> removeByChunk(const void* chunk);
    void requestFullRescan();
    std::optional<OreChunkScanTarget> selectNext(
            const void* levelIdentity, Vec3f cameraPosition,
            std::uint64_t nowMicroseconds) const;
    void markScanned(
            OreChunkScanTarget target, std::uint64_t nowMicroseconds);
    bool markRetry(
            OreChunkScanTarget target, std::uint64_t nowMicroseconds,
            std::uint64_t delayMicroseconds);
    std::size_t size() const;
    std::size_t sizeForLevel(const void* levelIdentity) const;
    std::size_t pendingForLevel(const void* levelIdentity) const;

private:
    struct Entry {
        OreChunkScanTarget target;
        std::uint64_t completedGeneration{};
        std::uint64_t lastScannedMicroseconds{};
        std::uint64_t retryNotBeforeMicroseconds{};
    };

    const std::size_t capacity_;
    const std::uint64_t periodicIntervalMicroseconds_;
    std::vector<Entry> entries_;
    std::uint64_t generation_{1};
    std::uint64_t nextPeriodicScanMicroseconds_{};
};

class ChunkOreRegistry {
public:
    ChunkOreRegistry(std::size_t chunkCapacity, std::size_t positionCapacity);

    ChunkOreUpdateResult replaceSubChunk(
            const void* levelIdentity, ChunkPosition chunk,
            std::int16_t absoluteSubChunk,
            const std::vector<OreBlock>& ores);
    ChunkOreUpdateResult replaceChunk(
            const void* levelIdentity, ChunkPosition chunk,
            std::span<const OreSubChunkSnapshot> subChunks);
    void removeChunk(const void* levelIdentity, ChunkPosition chunk);
    std::vector<OreBlock> snapshotNear(
        const void* levelIdentity, Vec3f cameraPosition,
        float maximumDistance, std::size_t maximumResults,
        const CameraFrame* camera = nullptr) const;
    std::size_t size() const;
    std::size_t sizeForLevel(const void* levelIdentity) const;
    std::size_t chunkCount() const;
    std::size_t chunkCountForLevel(const void* levelIdentity) const;
    void clear();

private:
    struct ChunkKey {
        std::uintptr_t level{};
        ChunkPosition position;

        bool operator==(const ChunkKey&) const = default;
    };

    struct ChunkKeyHash {
        std::size_t operator()(const ChunkKey& key) const;
    };

    struct ChunkEntry {
        std::unordered_map<std::int16_t, std::vector<OreBlock>> subChunks;
        std::size_t positionCount{};
    };

    const std::size_t chunkCapacity_;
    const std::size_t positionCapacity_;
    mutable std::mutex mutex_;
    std::unordered_map<ChunkKey, ChunkEntry, ChunkKeyHash> chunks_;
    std::size_t positionCount_{};
};

std::optional<OreKind> classifyOreBlockName(std::string_view name);
bool validBlockResourceName(std::string_view name);
OreBlockNameClassification classifyOreBlockNameForCache(
        std::optional<std::string_view> name);
std::optional<std::size_t> packedPaletteIndex(
        std::span<const std::uint32_t> words, std::uint8_t bitsPerElement,
        std::size_t elementIndex, std::size_t paletteSize);
std::string_view oreKindName(OreKind kind);
EntityAabb oreBlockBounds(BlockPosition position);
ChunkOreUpdateResult replaceClientChunkSubChunkOres(
        const void* levelIdentity, ChunkPosition chunk,
        std::int16_t absoluteSubChunk, const std::vector<OreBlock>& ores);
ChunkOreUpdateResult replaceClientChunkOres(
        const void* levelIdentity, ChunkPosition chunk,
        std::span<const OreSubChunkSnapshot> subChunks);
void removeClientChunkOres(const void* levelIdentity, ChunkPosition chunk);
std::vector<OreEspObservation> snapshotClientKnownOres(
        const void* levelIdentity, const CameraFrame& camera,
        std::size_t maximumResults);
std::size_t clientKnownOreCount();
std::size_t clientKnownOreCountForLevel(const void* levelIdentity);
std::size_t clientKnownOreChunkCount();
std::size_t clientKnownOreChunkCountForLevel(const void* levelIdentity);
void clearClientKnownOres();

} // namespace dobby
