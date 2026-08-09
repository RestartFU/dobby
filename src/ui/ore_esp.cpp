#include "ui/ore_esp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace dobby {
namespace {

constexpr std::size_t kMaximumClientChunks = 1'024;
constexpr std::size_t kMaximumClientOres = 131'072;
constexpr float kMaximumPresentationDistance = 256.0F;

struct NamedOre {
    std::string_view name;
    OreKind kind;
};

constexpr std::array<NamedOre, 21> kNamedOres{{
        {"minecraft:coal_ore", OreKind::coal},
        {"minecraft:deepslate_coal_ore", OreKind::coal},
        {"minecraft:iron_ore", OreKind::iron},
        {"minecraft:deepslate_iron_ore", OreKind::iron},
        {"minecraft:copper_ore", OreKind::copper},
        {"minecraft:deepslate_copper_ore", OreKind::copper},
        {"minecraft:gold_ore", OreKind::gold},
        {"minecraft:deepslate_gold_ore", OreKind::gold},
        {"minecraft:nether_gold_ore", OreKind::gold},
        {"minecraft:redstone_ore", OreKind::redstone},
        {"minecraft:lit_redstone_ore", OreKind::redstone},
        {"minecraft:deepslate_redstone_ore", OreKind::redstone},
        {"minecraft:lit_deepslate_redstone_ore", OreKind::redstone},
        {"minecraft:lapis_ore", OreKind::lapis},
        {"minecraft:deepslate_lapis_ore", OreKind::lapis},
        {"minecraft:diamond_ore", OreKind::diamond},
        {"minecraft:deepslate_diamond_ore", OreKind::diamond},
        {"minecraft:emerald_ore", OreKind::emerald},
        {"minecraft:deepslate_emerald_ore", OreKind::emerald},
        {"minecraft:quartz_ore", OreKind::quartz},
        {"minecraft:ancient_debris", OreKind::ancientDebris},
}};

ChunkOreRegistry clientKnownOres{kMaximumClientChunks, kMaximumClientOres};

bool oreBelongsToSubChunk(
        const OreBlock& ore, ChunkPosition chunk,
        std::int16_t absoluteSubChunk) {
    return validChestBlockPosition(ore.position) &&
            chunkPositionForBlock(ore.position) == chunk &&
            absoluteSubChunkForBlock(ore.position) == absoluteSubChunk &&
            ore.kind < OreKind::count;
}

float squaredDistance(Vec3f camera, BlockPosition position) {
    const float x = static_cast<float>(position.x) + 0.5F - camera.x;
    const float y = static_cast<float>(position.y) + 0.5F - camera.y;
    const float z = static_cast<float>(position.z) + 0.5F - camera.z;
    return x * x + y * y + z * z;
}

} // namespace

ChunkOreRegistry::ChunkOreRegistry(
        std::size_t chunkCapacity, std::size_t positionCapacity)
: chunkCapacity_(chunkCapacity), positionCapacity_(positionCapacity) {}

std::size_t ChunkOreRegistry::ChunkKeyHash::operator()(
        const ChunkKey& key) const {
    std::size_t value = std::hash<std::uintptr_t>{}(key.level);
    value ^= std::hash<std::int32_t>{}(key.position.x) + 0x9e3779b9U +
            (value << 6U) + (value >> 2U);
    value ^= std::hash<std::int32_t>{}(key.position.z) + 0x9e3779b9U +
            (value << 6U) + (value >> 2U);
    return value;
}

ChunkOreUpdateResult ChunkOreRegistry::replaceSubChunk(
        const void* levelIdentity, ChunkPosition chunk,
        std::int16_t absoluteSubChunk, const std::vector<OreBlock>& ores) {
    if (levelIdentity == nullptr || !validChunkPosition(chunk) ||
        !std::all_of(ores.begin(), ores.end(), [&](const OreBlock& ore) {
            return oreBelongsToSubChunk(ore, chunk, absoluteSubChunk);
        })) {
        return ChunkOreUpdateResult::invalidInput;
    }

    std::vector<OreBlock> uniqueOres = ores;
    std::sort(uniqueOres.begin(), uniqueOres.end(),
              [](const OreBlock& left, const OreBlock& right) {
                  if (left.position.y != right.position.y)
                      return left.position.y < right.position.y;
                  if (left.position.z != right.position.z)
                      return left.position.z < right.position.z;
                  if (left.position.x != right.position.x)
                      return left.position.x < right.position.x;
                  return left.kind < right.kind;
              });
    uniqueOres.erase(
            std::unique(uniqueOres.begin(), uniqueOres.end()),
            uniqueOres.end());

    const ChunkKey key{
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunk};
    std::lock_guard lock(mutex_);
    auto chunkEntry = chunks_.find(key);
    if (chunkEntry == chunks_.end() && !uniqueOres.empty() &&
        chunks_.size() >= chunkCapacity_) {
        return ChunkOreUpdateResult::chunkCapacityReached;
    }

    std::size_t previousCount = 0;
    if (chunkEntry != chunks_.end()) {
        const auto previous = chunkEntry->second.subChunks.find(
                absoluteSubChunk);
        if (previous != chunkEntry->second.subChunks.end())
            previousCount = previous->second.size();
    }
    if (uniqueOres.size() > positionCapacity_ ||
        positionCount_ - previousCount >
                positionCapacity_ - uniqueOres.size()) {
        return ChunkOreUpdateResult::positionCapacityReached;
    }

    if (chunkEntry == chunks_.end()) {
        if (uniqueOres.empty())
            return ChunkOreUpdateResult::accepted;
        chunkEntry = chunks_.emplace(key, ChunkEntry{}).first;
    }
    ChunkEntry& entry = chunkEntry->second;
    positionCount_ -= previousCount;
    entry.positionCount -= previousCount;
    if (uniqueOres.empty()) {
        entry.subChunks.erase(absoluteSubChunk);
    } else {
        entry.subChunks[absoluteSubChunk] = std::move(uniqueOres);
        const std::size_t added = entry.subChunks[absoluteSubChunk].size();
        entry.positionCount += added;
        positionCount_ += added;
    }
    if (entry.subChunks.empty())
        chunks_.erase(chunkEntry);
    return ChunkOreUpdateResult::accepted;
}

void ChunkOreRegistry::removeChunk(
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

std::vector<OreBlock> ChunkOreRegistry::snapshotNear(
        const void* levelIdentity, Vec3f cameraPosition,
        float maximumDistance, std::size_t maximumResults) const {
    std::vector<OreBlock> result;
    if (levelIdentity == nullptr || maximumResults == 0 ||
        !std::isfinite(maximumDistance) || maximumDistance <= 0.0F) {
        return result;
    }
    const auto level = reinterpret_cast<std::uintptr_t>(levelIdentity);
    const float maximumDistanceSquared = maximumDistance * maximumDistance;
    std::lock_guard lock(mutex_);
    result.reserve(std::min(maximumResults, positionCount_));
    for (const auto& [key, chunk] : chunks_) {
        if (key.level != level)
            continue;
        for (const auto& [absoluteIndex, ores] : chunk.subChunks) {
            static_cast<void>(absoluteIndex);
            for (const OreBlock& ore : ores) {
                if (squaredDistance(cameraPosition, ore.position) >
                    maximumDistanceSquared) {
                    continue;
                }
                result.push_back(ore);
                if (result.size() >= maximumResults)
                    return result;
            }
        }
    }
    return result;
}

std::size_t ChunkOreRegistry::size() const {
    std::lock_guard lock(mutex_);
    return positionCount_;
}

std::size_t ChunkOreRegistry::sizeForLevel(
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

void ChunkOreRegistry::clear() {
    std::lock_guard lock(mutex_);
    chunks_.clear();
    positionCount_ = 0;
}

std::optional<OreKind> classifyOreBlockName(std::string_view name) {
    const auto entry = std::find_if(
            kNamedOres.begin(), kNamedOres.end(),
            [&](const NamedOre& ore) { return ore.name == name; });
    if (entry != kNamedOres.end())
        return entry->kind;
    return std::nullopt;
}

bool validBlockResourceName(std::string_view name) {
    const auto separator = name.find(':');
    return separator != std::string_view::npos && separator != 0 &&
            separator + 1 < name.size() &&
            std::all_of(name.begin(), name.end(), [](char character) {
                return (character >= 'a' && character <= 'z') ||
                        (character >= '0' && character <= '9') ||
                        character == ':' || character == '_' ||
                        character == '.' || character == '-' ||
                        character == '/';
            });
}

std::optional<std::size_t> packedPaletteIndex(
        std::span<const std::uint32_t> words, std::uint8_t bitsPerElement,
        std::size_t elementIndex, std::size_t paletteSize) {
    constexpr std::size_t blocksPerSubChunk = 4'096;
    if (elementIndex >= blocksPerSubChunk || paletteSize == 0)
        return std::nullopt;
    if (bitsPerElement == 0)
        return words.empty() && paletteSize == 1
                ? std::optional<std::size_t>{0}
                : std::nullopt;
    const bool supported = bitsPerElement == 1 || bitsPerElement == 2 ||
            bitsPerElement == 3 || bitsPerElement == 4 ||
            bitsPerElement == 5 || bitsPerElement == 6 ||
            bitsPerElement == 8 || bitsPerElement == 16;
    if (!supported)
        return std::nullopt;

    const std::size_t elementsPerWord = 32U / bitsPerElement;
    const std::size_t wordIndex = elementIndex / elementsPerWord;
    if (wordIndex >= words.size())
        return std::nullopt;
    const std::size_t shift =
            (elementIndex % elementsPerWord) * bitsPerElement;
    const std::uint32_t mask =
            (std::uint32_t{1} << bitsPerElement) - 1U;
    const std::size_t paletteIndex =
            static_cast<std::size_t>((words[wordIndex] >> shift) & mask);
    return paletteIndex < paletteSize
            ? std::optional<std::size_t>{paletteIndex}
            : std::nullopt;
}

std::string_view oreKindName(OreKind kind) {
    constexpr std::array<std::string_view,
                         static_cast<std::size_t>(OreKind::count)> names{{
            "coal", "iron", "copper", "gold", "redstone", "lapis",
            "diamond", "emerald", "quartz", "ancient debris",
    }};
    const auto index = static_cast<std::size_t>(kind);
    return index < names.size() ? names[index] : std::string_view{"unknown"};
}

EntityAabb oreBlockBounds(BlockPosition position) {
    return chestBlockBounds(position);
}

ChunkOreUpdateResult replaceClientChunkSubChunkOres(
        const void* levelIdentity, ChunkPosition chunk,
        std::int16_t absoluteSubChunk, const std::vector<OreBlock>& ores) {
    return clientKnownOres.replaceSubChunk(
            levelIdentity, chunk, absoluteSubChunk, ores);
}

void removeClientChunkOres(
        const void* levelIdentity, ChunkPosition chunk) {
    clientKnownOres.removeChunk(levelIdentity, chunk);
}

std::vector<OreEspObservation> snapshotClientKnownOres(
        const void* levelIdentity, const CameraFrame& camera,
        std::size_t maximumResults) {
    std::vector<OreEspObservation> result;
    if (!validCameraFrame(camera))
        return result;
    const auto ores = clientKnownOres.snapshotNear(
            levelIdentity, camera.position, kMaximumPresentationDistance,
            maximumResults);
    result.reserve(ores.size());
    for (const OreBlock& ore : ores)
        result.push_back({oreBlockBounds(ore.position), ore.kind});
    return result;
}

std::size_t clientKnownOreCount() {
    return clientKnownOres.size();
}

std::size_t clientKnownOreCountForLevel(const void* levelIdentity) {
    return clientKnownOres.sizeForLevel(levelIdentity);
}

void clearClientKnownOres() {
    clientKnownOres.clear();
}

} // namespace dobby
