#pragma once

#include "ui/entity_hitbox_overlay.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dobby {

struct BlockPosition {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t z{};

    bool operator==(const BlockPosition&) const = default;
};

struct ChunkPosition {
    std::int32_t x{};
    std::int32_t z{};

    bool operator==(const ChunkPosition&) const = default;
};

struct ChestEspObservation {
    EntityAabb bounds;
    CameraFrame camera;
};

enum class ChunkChestUpdateResult {
    accepted,
    invalidInput,
    chunkCapacityReached,
    positionCapacityReached,
};

class ChunkChestRegistry {
public:
    ChunkChestRegistry(std::size_t chunkCapacity, std::size_t positionCapacity);

    ChunkChestUpdateResult replaceSubChunk(
            const void* levelIdentity, ChunkPosition chunk,
            std::int16_t absoluteSubChunk,
            const std::vector<BlockPosition>& positions);
    ChunkChestUpdateResult add(
            const void* levelIdentity, BlockPosition position);
    void remove(const void* levelIdentity, BlockPosition position);
    void removeChunk(const void* levelIdentity, ChunkPosition chunk);
    std::vector<BlockPosition> snapshot(const void* levelIdentity) const;
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
        std::unordered_map<std::int16_t, std::vector<BlockPosition>> subChunks;
        std::size_t positionCount{};
    };

    const std::size_t chunkCapacity_;
    const std::size_t positionCapacity_;
    mutable std::mutex mutex_;
    std::unordered_map<ChunkKey, ChunkEntry, ChunkKeyHash> chunks_;
    std::size_t positionCount_{};
};

bool validChestBlockPosition(BlockPosition position);
bool validChunkPosition(ChunkPosition position);
ChunkPosition chunkPositionForBlock(BlockPosition position);
std::int16_t absoluteSubChunkForBlock(BlockPosition position);
std::uint16_t subChunkStorageIndex(
        std::uint8_t localX, std::uint8_t localY, std::uint8_t localZ);
BlockPosition worldBlockPosition(
        ChunkPosition chunk, std::int16_t absoluteSubChunk,
        std::uint8_t localX, std::uint8_t localY, std::uint8_t localZ);
EntityAabb chestBlockBounds(BlockPosition position);
ChunkChestUpdateResult replaceClientChunkSubChunkChests(
        const void* levelIdentity, ChunkPosition chunk,
        std::int16_t absoluteSubChunk,
        const std::vector<BlockPosition>& positions);
ChunkChestUpdateResult addClientKnownChest(
        const void* levelIdentity, BlockPosition position);
void removeClientKnownChest(
        const void* levelIdentity, BlockPosition position);
void removeClientChunkChests(
        const void* levelIdentity, ChunkPosition chunk);
std::vector<ChestEspObservation> snapshotClientKnownChests(
        const void* levelIdentity, const CameraFrame& camera);
std::size_t clientKnownChestCount();
std::size_t clientKnownChestCountForLevel(const void* levelIdentity);
void clearClientKnownChests();

} // namespace dobby
