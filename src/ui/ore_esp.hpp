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

struct OreBlock {
    BlockPosition position;
    OreKind kind{};

    bool operator==(const OreBlock&) const = default;
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

class ChunkOreRegistry {
public:
    ChunkOreRegistry(std::size_t chunkCapacity, std::size_t positionCapacity);

    ChunkOreUpdateResult replaceSubChunk(
            const void* levelIdentity, ChunkPosition chunk,
            std::int16_t absoluteSubChunk,
            const std::vector<OreBlock>& ores);
    void removeChunk(const void* levelIdentity, ChunkPosition chunk);
    std::vector<OreBlock> snapshotNear(
            const void* levelIdentity, Vec3f cameraPosition,
            float maximumDistance, std::size_t maximumResults) const;
    std::size_t size() const;
    std::size_t sizeForLevel(const void* levelIdentity) const;
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
std::optional<std::size_t> packedPaletteIndex(
        std::span<const std::uint32_t> words, std::uint8_t bitsPerElement,
        std::size_t elementIndex, std::size_t paletteSize);
std::string_view oreKindName(OreKind kind);
EntityAabb oreBlockBounds(BlockPosition position);
ChunkOreUpdateResult replaceClientChunkSubChunkOres(
        const void* levelIdentity, ChunkPosition chunk,
        std::int16_t absoluteSubChunk, const std::vector<OreBlock>& ores);
void removeClientChunkOres(const void* levelIdentity, ChunkPosition chunk);
std::vector<OreEspObservation> snapshotClientKnownOres(
        const void* levelIdentity, const CameraFrame& camera,
        std::size_t maximumResults);
std::size_t clientKnownOreCount();
std::size_t clientKnownOreCountForLevel(const void* levelIdentity);
void clearClientKnownOres();

} // namespace dobby
