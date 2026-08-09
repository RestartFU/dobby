#include "ui/chest_esp.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace dobby {
namespace {

constexpr std::int32_t kMaximumHorizontalCoordinate = 30'000'000;
constexpr std::int32_t kMinimumVerticalCoordinate = -4'096;
constexpr std::int32_t kMaximumVerticalCoordinate = 4'096;
constexpr std::size_t kMaximumClientChunks = 1'024;
constexpr std::size_t kMaximumClientChests = 8'192;

ChunkChestRegistry clientKnownChests{
        kMaximumClientChunks, kMaximumClientChests};

std::int32_t floorDiv16(std::int32_t value) {
    if (value >= 0)
        return value / 16;
    return -static_cast<std::int32_t>(
            (static_cast<std::int64_t>(-value) + 15) / 16);
}

bool positionBelongsToSubChunk(
        BlockPosition position, ChunkPosition chunk,
        std::int16_t absoluteSubChunk) {
    return validChestBlockPosition(position) &&
            floorDiv16(position.x) == chunk.x &&
            floorDiv16(position.z) == chunk.z &&
            floorDiv16(position.y) == absoluteSubChunk;
}

} // namespace

ChunkChestRegistry::ChunkChestRegistry(
        std::size_t chunkCapacity, std::size_t positionCapacity)
: chunkCapacity_(chunkCapacity), positionCapacity_(positionCapacity) {}

std::size_t ChunkChestRegistry::ChunkKeyHash::operator()(
        const ChunkKey& key) const {
    std::size_t value = std::hash<std::uintptr_t>{}(key.level);
    value ^= std::hash<std::int32_t>{}(key.position.x) + 0x9e3779b9U +
            (value << 6U) + (value >> 2U);
    value ^= std::hash<std::int32_t>{}(key.position.z) + 0x9e3779b9U +
            (value << 6U) + (value >> 2U);
    return value;
}

ChunkChestUpdateResult ChunkChestRegistry::replaceSubChunk(
        const void* levelIdentity, ChunkPosition chunk,
        std::int16_t absoluteSubChunk,
        const std::vector<BlockPosition>& positions) {
    if (levelIdentity == nullptr || !validChunkPosition(chunk) ||
        !std::all_of(positions.begin(), positions.end(),
                     [&](BlockPosition position) {
                         return positionBelongsToSubChunk(
                                 position, chunk, absoluteSubChunk);
                     })) {
        return ChunkChestUpdateResult::invalidInput;
    }

    std::vector<BlockPosition> uniquePositions = positions;
    std::sort(uniquePositions.begin(), uniquePositions.end(),
              [](BlockPosition left, BlockPosition right) {
                  if (left.y != right.y)
                      return left.y < right.y;
                  if (left.z != right.z)
                      return left.z < right.z;
                  return left.x < right.x;
              });
    uniquePositions.erase(
            std::unique(uniquePositions.begin(), uniquePositions.end()),
            uniquePositions.end());

    const ChunkKey key{
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunk};
    std::lock_guard lock(mutex_);
    auto chunkEntry = chunks_.find(key);
    if (chunkEntry == chunks_.end()) {
        if (uniquePositions.empty())
            return ChunkChestUpdateResult::accepted;
        if (chunks_.size() >= chunkCapacity_)
            return ChunkChestUpdateResult::chunkCapacityReached;
    }

    std::size_t previousCount = 0;
    if (chunkEntry != chunks_.end()) {
        const auto previous = chunkEntry->second.subChunks.find(
                absoluteSubChunk);
        if (previous != chunkEntry->second.subChunks.end())
            previousCount = previous->second.size();
    }
    if (uniquePositions.size() > positionCapacity_ ||
        positionCount_ - previousCount >
                positionCapacity_ - uniquePositions.size()) {
        return ChunkChestUpdateResult::positionCapacityReached;
    }

    if (chunkEntry == chunks_.end())
        chunkEntry = chunks_.emplace(key, ChunkEntry{}).first;
    ChunkEntry& entry = chunkEntry->second;
    positionCount_ -= previousCount;
    entry.positionCount -= previousCount;
    if (uniquePositions.empty()) {
        entry.subChunks.erase(absoluteSubChunk);
    } else {
        entry.subChunks[absoluteSubChunk] = std::move(uniquePositions);
        const std::size_t added = entry.subChunks[absoluteSubChunk].size();
        entry.positionCount += added;
        positionCount_ += added;
    }
    if (entry.subChunks.empty())
        chunks_.erase(chunkEntry);
    return ChunkChestUpdateResult::accepted;
}

ChunkChestUpdateResult ChunkChestRegistry::add(
        const void* levelIdentity, BlockPosition position) {
    if (levelIdentity == nullptr || !validChestBlockPosition(position))
        return ChunkChestUpdateResult::invalidInput;

    const ChunkPosition chunk = chunkPositionForBlock(position);
    const std::int16_t absoluteSubChunk = absoluteSubChunkForBlock(position);
    const ChunkKey key{
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunk};
    std::lock_guard lock(mutex_);
    auto chunkEntry = chunks_.find(key);
    if (chunkEntry == chunks_.end()) {
        if (chunks_.size() >= chunkCapacity_)
            return ChunkChestUpdateResult::chunkCapacityReached;
        if (positionCount_ >= positionCapacity_)
            return ChunkChestUpdateResult::positionCapacityReached;
        chunkEntry = chunks_.emplace(key, ChunkEntry{}).first;
    }

    auto& positions = chunkEntry->second.subChunks[absoluteSubChunk];
    if (std::find(positions.begin(), positions.end(), position) !=
        positions.end()) {
        return ChunkChestUpdateResult::accepted;
    }
    if (positionCount_ >= positionCapacity_) {
        if (positions.empty())
            chunkEntry->second.subChunks.erase(absoluteSubChunk);
        if (chunkEntry->second.subChunks.empty())
            chunks_.erase(chunkEntry);
        return ChunkChestUpdateResult::positionCapacityReached;
    }
    positions.push_back(position);
    ++chunkEntry->second.positionCount;
    ++positionCount_;
    return ChunkChestUpdateResult::accepted;
}

void ChunkChestRegistry::remove(
        const void* levelIdentity, BlockPosition position) {
    if (levelIdentity == nullptr || !validChestBlockPosition(position))
        return;
    const ChunkPosition chunk = chunkPositionForBlock(position);
    const std::int16_t absoluteSubChunk = absoluteSubChunkForBlock(position);
    const ChunkKey key{
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunk};
    std::lock_guard lock(mutex_);
    const auto chunkEntry = chunks_.find(key);
    if (chunkEntry == chunks_.end())
        return;
    const auto subChunk = chunkEntry->second.subChunks.find(absoluteSubChunk);
    if (subChunk == chunkEntry->second.subChunks.end())
        return;
    const auto positionEntry = std::find(
            subChunk->second.begin(), subChunk->second.end(), position);
    if (positionEntry == subChunk->second.end())
        return;
    subChunk->second.erase(positionEntry);
    --chunkEntry->second.positionCount;
    --positionCount_;
    if (subChunk->second.empty())
        chunkEntry->second.subChunks.erase(subChunk);
    if (chunkEntry->second.subChunks.empty())
        chunks_.erase(chunkEntry);
}

void ChunkChestRegistry::removeChunk(
        const void* levelIdentity, ChunkPosition chunk) {
    if (levelIdentity == nullptr)
        return;
    const ChunkKey key{
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunk};
    std::lock_guard lock(mutex_);
    const auto entry = chunks_.find(key);
    if (entry == chunks_.end())
        return;
    positionCount_ -= entry->second.positionCount;
    chunks_.erase(entry);
}

std::vector<BlockPosition> ChunkChestRegistry::snapshot(
        const void* levelIdentity) const {
    std::vector<BlockPosition> result;
    if (levelIdentity == nullptr)
        return result;
    const auto level = reinterpret_cast<std::uintptr_t>(levelIdentity);
    std::lock_guard lock(mutex_);
    result.reserve(positionCount_);
    for (const auto& [key, chunk] : chunks_) {
        if (key.level != level)
            continue;
        for (const auto& [absoluteIndex, positions] : chunk.subChunks) {
            static_cast<void>(absoluteIndex);
            result.insert(result.end(), positions.begin(), positions.end());
        }
    }
    return result;
}

std::size_t ChunkChestRegistry::size() const {
    std::lock_guard lock(mutex_);
    return positionCount_;
}

std::size_t ChunkChestRegistry::sizeForLevel(
        const void* levelIdentity) const {
    if (levelIdentity == nullptr)
        return 0;
    const auto level = reinterpret_cast<std::uintptr_t>(levelIdentity);
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& [key, chunk] : chunks_) {
        if (key.level == level)
            count += chunk.positionCount;
    }
    return count;
}

void ChunkChestRegistry::clear() {
    std::lock_guard lock(mutex_);
    chunks_.clear();
    positionCount_ = 0;
}

bool validChestBlockPosition(BlockPosition position) {
    return std::abs(static_cast<std::int64_t>(position.x)) <=
                    kMaximumHorizontalCoordinate &&
            std::abs(static_cast<std::int64_t>(position.z)) <=
                    kMaximumHorizontalCoordinate &&
            position.y >= kMinimumVerticalCoordinate &&
            position.y <= kMaximumVerticalCoordinate;
}

bool validChunkPosition(ChunkPosition position) {
    constexpr std::int32_t maximumChunkCoordinate =
            kMaximumHorizontalCoordinate / 16 + 1;
    return std::abs(static_cast<std::int64_t>(position.x)) <=
                    maximumChunkCoordinate &&
            std::abs(static_cast<std::int64_t>(position.z)) <=
                    maximumChunkCoordinate;
}

ChunkPosition chunkPositionForBlock(BlockPosition position) {
    return {floorDiv16(position.x), floorDiv16(position.z)};
}

std::int16_t absoluteSubChunkForBlock(BlockPosition position) {
    const auto value = floorDiv16(position.y);
    return static_cast<std::int16_t>(std::clamp(
            value,
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
}

std::uint16_t subChunkStorageIndex(
        std::uint8_t localX, std::uint8_t localY, std::uint8_t localZ) {
    return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(localX & 0x0fU) << 8U) |
            (static_cast<std::uint16_t>(localZ & 0x0fU) << 4U) |
            static_cast<std::uint16_t>(localY & 0x0fU));
}

BlockPosition worldBlockPosition(
        ChunkPosition chunk, std::int16_t absoluteSubChunk,
        std::uint8_t localX, std::uint8_t localY, std::uint8_t localZ) {
    const auto worldX = static_cast<std::int64_t>(chunk.x) * 16 + localX;
    const auto worldY = static_cast<std::int64_t>(absoluteSubChunk) * 16 + localY;
    const auto worldZ = static_cast<std::int64_t>(chunk.z) * 16 + localZ;
    const auto clamp = [](std::int64_t value) {
        return static_cast<std::int32_t>(std::clamp(
                value,
                static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
                static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
    };
    return {clamp(worldX), clamp(worldY), clamp(worldZ)};
}

EntityAabb chestBlockBounds(BlockPosition position) {
    const Vec3f minimum{
            static_cast<float>(position.x), static_cast<float>(position.y),
            static_cast<float>(position.z)};
    return {minimum, {minimum.x + 1.0F, minimum.y + 1.0F, minimum.z + 1.0F}};
}

ChunkChestUpdateResult replaceClientChunkSubChunkChests(
        const void* levelIdentity, ChunkPosition chunk,
        std::int16_t absoluteSubChunk,
        const std::vector<BlockPosition>& positions) {
    return clientKnownChests.replaceSubChunk(
            levelIdentity, chunk, absoluteSubChunk, positions);
}

ChunkChestUpdateResult addClientKnownChest(
        const void* levelIdentity, BlockPosition position) {
    return clientKnownChests.add(levelIdentity, position);
}

void removeClientKnownChest(
        const void* levelIdentity, BlockPosition position) {
    clientKnownChests.remove(levelIdentity, position);
}

void removeClientChunkChests(
        const void* levelIdentity, ChunkPosition chunk) {
    clientKnownChests.removeChunk(levelIdentity, chunk);
}

std::vector<ChestEspObservation> snapshotClientKnownChests(
        const void* levelIdentity, const CameraFrame& camera) {
    std::vector<ChestEspObservation> result;
    if (!validCameraFrame(camera))
        return result;
    const auto positions = clientKnownChests.snapshot(levelIdentity);
    result.reserve(positions.size());
    for (const BlockPosition position : positions)
        result.push_back({chestBlockBounds(position), camera});
    return result;
}

std::size_t clientKnownChestCount() {
    return clientKnownChests.size();
}

std::size_t clientKnownChestCountForLevel(const void* levelIdentity) {
    return clientKnownChests.sizeForLevel(levelIdentity);
}

void clearClientKnownChests() {
    clientKnownChests.clear();
}

} // namespace dobby
