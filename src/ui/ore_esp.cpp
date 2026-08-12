#include "ui/ore_esp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace dobby {
namespace {

constexpr std::size_t kMaximumClientChunks = 4'096;
constexpr std::size_t kMaximumClientOres = 262'144;
constexpr std::size_t kMaximumSubChunksPerChunk = 64;
constexpr std::size_t kMaximumBlocksPerSubChunk = 4'096;
constexpr float kMaximumPresentationDistance = 256.0F;

struct NamedOre {
    std::string_view name;
    OreKind kind;
};

constexpr std::array<NamedOre, 34> kNamedOres{{
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
        {"minecraft:coal_block", OreKind::coal},
        {"minecraft:iron_block", OreKind::iron},
        {"minecraft:raw_iron_block", OreKind::iron},
        {"minecraft:copper_block", OreKind::copper},
        {"minecraft:raw_copper_block", OreKind::copper},
        {"minecraft:gold_block", OreKind::gold},
        {"minecraft:raw_gold_block", OreKind::gold},
        {"minecraft:redstone_block", OreKind::redstone},
        {"minecraft:lapis_block", OreKind::lapis},
        {"minecraft:diamond_block", OreKind::diamond},
        {"minecraft:emerald_block", OreKind::emerald},
        {"minecraft:quartz_block", OreKind::quartz},
        {"minecraft:netherite_block", OreKind::ancientDebris},
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

float squaredChunkDistance(Vec3f camera, ChunkPosition position) {
    const float x = static_cast<float>(position.x * 16 + 8) - camera.x;
    const float z = static_cast<float>(position.z * 16 + 8) - camera.z;
    return x * x + z * z;
}

float squaredHorizontalDistanceToChunk(
        Vec3f camera, ChunkPosition position) {
    const float minimumX = static_cast<float>(position.x * 16);
    const float maximumX = minimumX + 16.0F;
    const float minimumZ = static_cast<float>(position.z * 16);
    const float maximumZ = minimumZ + 16.0F;
    const float x = camera.x < minimumX
            ? minimumX - camera.x
            : camera.x > maximumX ? camera.x - maximumX : 0.0F;
    const float z = camera.z < minimumZ
            ? minimumZ - camera.z
            : camera.z > maximumZ ? camera.z - maximumZ : 0.0F;
    return x * x + z * z;
}

ChunkPosition cameraChunk(Vec3f camera) {
    return {
            static_cast<std::int32_t>(std::floor(camera.x / 16.0F)),
            static_cast<std::int32_t>(std::floor(camera.z / 16.0F)),
    };
}

std::uint64_t saturatingAdd(
        std::uint64_t left, std::uint64_t right) {
    return left > std::numeric_limits<std::uint64_t>::max() - right
            ? std::numeric_limits<std::uint64_t>::max()
            : left + right;
}

bool orePositionLess(const OreBlock& left, const OreBlock& right) {
    if (left.position.y != right.position.y)
        return left.position.y < right.position.y;
    if (left.position.z != right.position.z)
        return left.position.z < right.position.z;
    if (left.position.x != right.position.x)
        return left.position.x < right.position.x;
    return left.kind < right.kind;
}

bool normalizeSubChunkOres(
        std::span<const OreBlock> ores, ChunkPosition chunk,
        std::int16_t absoluteSubChunk, std::vector<OreBlock>& output) {
    if (ores.size() > kMaximumBlocksPerSubChunk ||
        !std::all_of(ores.begin(), ores.end(), [&](const OreBlock& ore) {
            return oreBelongsToSubChunk(ore, chunk, absoluteSubChunk);
        })) {
        return false;
    }
    output.assign(ores.begin(), ores.end());
    std::sort(output.begin(), output.end(), orePositionLess);
    output.erase(std::unique(output.begin(), output.end()), output.end());
    return true;
}

} // namespace

OreRescanSchedule::OreRescanSchedule(
        std::size_t capacity,
        std::uint64_t periodicIntervalMicroseconds)
: capacity_(capacity),
  periodicIntervalMicroseconds_(periodicIntervalMicroseconds) {}

OreChunkTrackResult OreRescanSchedule::track(
        OreChunkScanTarget target,
        std::optional<OreChunkScanTarget>* evictedTarget) {
    if (evictedTarget != nullptr)
        evictedTarget->reset();
    if (target.chunk == nullptr || target.level == nullptr ||
        !validChunkPosition(target.position)) {
        return OreChunkTrackResult::invalidInput;
    }
    const auto existing = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& entry) {
                return entry.target.level == target.level &&
                        entry.target.position == target.position;
            });
    if (existing != entries_.end() && existing->target.chunk == target.chunk)
        return OreChunkTrackResult::accepted;
    if (existing != entries_.end()) {
        entries_.erase(existing);
        entries_.push_back({target, 0, 0, 0});
        return OreChunkTrackResult::accepted;
    }
    if (capacity_ == 0)
        return OreChunkTrackResult::capacityReached;
    if (entries_.size() >= capacity_) {
        if (evictedTarget != nullptr)
            *evictedTarget = entries_.front().target;
        entries_.erase(entries_.begin());
        entries_.push_back({target, 0, 0, 0});
        return OreChunkTrackResult::acceptedWithEviction;
    }
    entries_.push_back({target, 0, 0, 0});
    return OreChunkTrackResult::accepted;
}

OreChunkTrackResult OreRescanSchedule::trackIfAbsent(
        OreChunkScanTarget target,
        std::optional<OreChunkScanTarget>* evictedTarget) {
    if (evictedTarget != nullptr)
        evictedTarget->reset();
    if (target.chunk == nullptr || target.level == nullptr ||
        !validChunkPosition(target.position)) {
        return OreChunkTrackResult::invalidInput;
    }
    const auto existing = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& entry) {
                return entry.target.level == target.level &&
                        entry.target.position == target.position;
            });
    if (existing != entries_.end()) {
        return existing->target.chunk == target.chunk
                ? OreChunkTrackResult::accepted
                : OreChunkTrackResult::existingOwner;
    }
    if (capacity_ == 0)
        return OreChunkTrackResult::capacityReached;
    if (entries_.size() >= capacity_) {
        if (evictedTarget != nullptr)
            *evictedTarget = entries_.front().target;
        entries_.erase(entries_.begin());
        entries_.push_back({target, 0, 0, 0});
        return OreChunkTrackResult::acceptedWithEviction;
    }
    entries_.push_back({target, 0, 0, 0});
    return OreChunkTrackResult::accepted;
}

bool OreRescanSchedule::markDirty(OreChunkScanTarget target) {
    const auto entry = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& candidate) {
                return candidate.target == target;
            });
    if (entry == entries_.end())
        return false;
    const bool alreadyPending =
            entry->completedGeneration != generation_;
    entry->completedGeneration = 0;
    if (!alreadyPending)
        entry->retryNotBeforeMicroseconds = 0;
    return true;
}

bool OreRescanSchedule::remove(OreChunkScanTarget target) {
    const auto entry = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& candidate) {
                return candidate.target == target;
            });
    if (entry == entries_.end())
        return false;
    entries_.erase(entry);
    return true;
}

std::optional<OreChunkScanTarget> OreRescanSchedule::removeByChunk(
        const void* chunk) {
    if (chunk == nullptr)
        return std::nullopt;
    const auto entry = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& candidate) {
                return candidate.target.chunk == chunk;
            });
    if (entry == entries_.end())
        return std::nullopt;
    const OreChunkScanTarget removed = entry->target;
    entries_.erase(entry);
    return removed;
}

void OreRescanSchedule::requestFullRescan() {
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        generation_ = 1;
        for (Entry& entry : entries_)
            entry.completedGeneration = 0;
    } else {
        ++generation_;
    }
    for (Entry& entry : entries_)
        entry.retryNotBeforeMicroseconds = 0;
    nextPeriodicScanMicroseconds_ = 0;
}

std::optional<OreChunkScanTarget> OreRescanSchedule::selectNext(
        const void* levelIdentity, Vec3f cameraPosition,
        std::uint64_t nowMicroseconds) const {
    if (levelIdentity == nullptr || !std::isfinite(cameraPosition.x) ||
        !std::isfinite(cameraPosition.y) ||
        !std::isfinite(cameraPosition.z)) {
        return std::nullopt;
    }
    const Entry* selected = nullptr;
    float selectedDistance = std::numeric_limits<float>::infinity();
    for (const Entry& entry : entries_) {
        if (entry.target.level != levelIdentity ||
            entry.completedGeneration == generation_ ||
            nowMicroseconds < entry.retryNotBeforeMicroseconds) {
            continue;
        }
        const float distance =
                squaredChunkDistance(cameraPosition, entry.target.position);
        if (selected == nullptr || distance < selectedDistance) {
            selected = &entry;
            selectedDistance = distance;
        }
    }
    if (selected != nullptr)
        return selected->target;

    if (nowMicroseconds < nextPeriodicScanMicroseconds_)
        return std::nullopt;
    const ChunkPosition current = cameraChunk(cameraPosition);
    for (const Entry& entry : entries_) {
        if (entry.target.level != levelIdentity ||
            entry.completedGeneration != generation_ ||
            std::abs(entry.target.position.x - current.x) > 1 ||
            std::abs(entry.target.position.z - current.z) > 1) {
            continue;
        }
        const float distance =
                squaredChunkDistance(cameraPosition, entry.target.position);
        if (selected == nullptr ||
            entry.lastScannedMicroseconds <
                    selected->lastScannedMicroseconds ||
            (entry.lastScannedMicroseconds ==
                     selected->lastScannedMicroseconds &&
             distance < selectedDistance)) {
            selected = &entry;
            selectedDistance = distance;
        }
    }
    return selected == nullptr
            ? std::nullopt
            : std::optional<OreChunkScanTarget>{selected->target};
}

void OreRescanSchedule::markScanned(
        OreChunkScanTarget target, std::uint64_t nowMicroseconds) {
    const auto entry = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& candidate) {
                return candidate.target == target;
            });
    if (entry == entries_.end())
        return;
    entry->completedGeneration = generation_;
    entry->lastScannedMicroseconds = nowMicroseconds;
    entry->retryNotBeforeMicroseconds = 0;
    nextPeriodicScanMicroseconds_ = saturatingAdd(
            nowMicroseconds, periodicIntervalMicroseconds_);
}

bool OreRescanSchedule::markRetry(
        OreChunkScanTarget target, std::uint64_t nowMicroseconds,
        std::uint64_t delayMicroseconds) {
    const auto entry = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& candidate) {
                return candidate.target == target;
            });
    if (entry == entries_.end())
        return false;
    entry->completedGeneration = 0;
    entry->retryNotBeforeMicroseconds = saturatingAdd(
            nowMicroseconds, delayMicroseconds);
    return true;
}

std::size_t OreRescanSchedule::size() const {
    return entries_.size();
}

std::size_t OreRescanSchedule::sizeForLevel(
        const void* levelIdentity) const {
    return static_cast<std::size_t>(std::count_if(
            entries_.begin(), entries_.end(), [&](const Entry& entry) {
                return entry.target.level == levelIdentity;
            }));
}

std::size_t OreRescanSchedule::pendingForLevel(
        const void* levelIdentity) const {
    return static_cast<std::size_t>(std::count_if(
            entries_.begin(), entries_.end(), [&](const Entry& entry) {
                return entry.target.level == levelIdentity &&
                        entry.completedGeneration != generation_;
            }));
}

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
    if (levelIdentity == nullptr || !validChunkPosition(chunk)) {
        return ChunkOreUpdateResult::invalidInput;
    }

    std::vector<OreBlock> uniqueOres;
    if (!normalizeSubChunkOres(
                ores, chunk, absoluteSubChunk, uniqueOres)) {
        return ChunkOreUpdateResult::invalidInput;
    }

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

ChunkOreUpdateResult ChunkOreRegistry::replaceChunk(
        const void* levelIdentity, ChunkPosition chunk,
        std::span<const OreSubChunkSnapshot> subChunks) {
    if (levelIdentity == nullptr || !validChunkPosition(chunk) ||
        subChunks.size() > kMaximumSubChunksPerChunk) {
        return ChunkOreUpdateResult::invalidInput;
    }

    ChunkEntry replacement;
    replacement.subChunks.reserve(subChunks.size());
    for (const OreSubChunkSnapshot& subChunk : subChunks) {
        if (replacement.subChunks.contains(subChunk.absoluteSubChunk))
            return ChunkOreUpdateResult::invalidInput;
        std::vector<OreBlock> uniqueOres;
        if (!normalizeSubChunkOres(
                    subChunk.ores, chunk, subChunk.absoluteSubChunk,
                    uniqueOres)) {
            return ChunkOreUpdateResult::invalidInput;
        }
        if (uniqueOres.size() > positionCapacity_ ||
            replacement.positionCount >
                    positionCapacity_ - uniqueOres.size()) {
            return ChunkOreUpdateResult::positionCapacityReached;
        }
        replacement.positionCount += uniqueOres.size();
        replacement.subChunks.emplace(
                subChunk.absoluteSubChunk, std::move(uniqueOres));
    }
    for (auto entry = replacement.subChunks.begin();
         entry != replacement.subChunks.end();) {
        if (entry->second.empty()) {
            entry = replacement.subChunks.erase(entry);
        } else {
            ++entry;
        }
    }

    const ChunkKey key{
            reinterpret_cast<std::uintptr_t>(levelIdentity), chunk};
    std::lock_guard lock(mutex_);
    const auto current = chunks_.find(key);
    if (current == chunks_.end() && !replacement.subChunks.empty() &&
        chunks_.size() >= chunkCapacity_) {
        return ChunkOreUpdateResult::chunkCapacityReached;
    }
    const std::size_t previousCount = current == chunks_.end()
            ? 0
            : current->second.positionCount;
    if (replacement.positionCount > positionCapacity_ ||
        positionCount_ - previousCount >
                positionCapacity_ - replacement.positionCount) {
        return ChunkOreUpdateResult::positionCapacityReached;
    }

    const std::size_t updatedPositionCount =
            positionCount_ - previousCount + replacement.positionCount;
    if (replacement.subChunks.empty()) {
        if (current != chunks_.end())
            chunks_.erase(current);
    } else if (current == chunks_.end()) {
        chunks_.emplace(key, std::move(replacement));
    } else {
        current->second = std::move(replacement);
    }
    positionCount_ = updatedPositionCount;
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
        float maximumDistance, std::size_t maximumResults,
        const CameraFrame* camera) const {
    std::vector<OreBlock> result;
    if (levelIdentity == nullptr || maximumResults == 0 ||
        !std::isfinite(cameraPosition.x) ||
        !std::isfinite(cameraPosition.y) ||
        !std::isfinite(cameraPosition.z) ||
        !std::isfinite(maximumDistance) || maximumDistance <= 0.0F) {
        return result;
    }
    const auto level = reinterpret_cast<std::uintptr_t>(levelIdentity);
    const float maximumDistanceSquared = maximumDistance * maximumDistance;
    struct Candidate {
        OreBlock ore;
        float squaredDistance{};
        bool withinViewport{};
    };
    std::vector<Candidate> candidates;
    {
        std::lock_guard lock(mutex_);
        candidates.reserve(std::min(positionCount_, positionCapacity_));
        for (const auto& [key, chunk] : chunks_) {
            if (key.level != level ||
                squaredHorizontalDistanceToChunk(
                        cameraPosition, key.position) > maximumDistanceSquared) {
                continue;
            }
            for (const auto& [absoluteIndex, ores] : chunk.subChunks) {
                static_cast<void>(absoluteIndex);
                for (const OreBlock& ore : ores) {
                    const float distance =
                            squaredDistance(cameraPosition, ore.position);
                    if (distance > maximumDistanceSquared)
                        continue;
                    const Vec3f center{
                            static_cast<float>(ore.position.x) + 0.5F,
                            static_cast<float>(ore.position.y) + 0.5F,
                            static_cast<float>(ore.position.z) + 0.5F,
                    };
                    candidates.push_back({
                            ore, distance,
                            camera != nullptr &&
                                    worldPointWithinViewport(
                                            *camera, center)});
                }
            }
        }
    }
    const auto candidateLess = [](const Candidate& left,
                                  const Candidate& right) {
        if (left.withinViewport != right.withinViewport)
            return left.withinViewport > right.withinViewport;
        if (left.squaredDistance != right.squaredDistance)
            return left.squaredDistance < right.squaredDistance;
        return orePositionLess(left.ore, right.ore);
    };
    const std::size_t selected =
            std::min(maximumResults, candidates.size());
    const auto selectedEnd = candidates.begin() +
            static_cast<std::vector<Candidate>::difference_type>(selected);
    std::partial_sort(
            candidates.begin(), selectedEnd, candidates.end(), candidateLess);
    result.reserve(selected);
    for (std::size_t index = 0; index < selected; ++index)
        result.push_back(candidates[index].ore);
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

std::size_t ChunkOreRegistry::chunkCount() const {
    std::lock_guard lock(mutex_);
    return chunks_.size();
}

std::size_t ChunkOreRegistry::chunkCountForLevel(
        const void* levelIdentity) const {
    if (levelIdentity == nullptr)
        return 0;
    const auto level = reinterpret_cast<std::uintptr_t>(levelIdentity);
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
            chunks_.begin(), chunks_.end(), [&](const auto& entry) {
                return entry.first.level == level;
            }));
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

OreBlockNameClassification classifyOreBlockNameForCache(
        std::optional<std::string_view> name) {
    if (!name || !validBlockResourceName(*name))
        return {};
    return {true, classifyOreBlockName(*name)};
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

ChunkOreUpdateResult replaceClientChunkOres(
        const void* levelIdentity, ChunkPosition chunk,
        std::span<const OreSubChunkSnapshot> subChunks) {
    return clientKnownOres.replaceChunk(levelIdentity, chunk, subChunks);
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
            maximumResults, &camera);
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

std::size_t clientKnownOreChunkCount() {
    return clientKnownOres.chunkCount();
}

std::size_t clientKnownOreChunkCountForLevel(
        const void* levelIdentity) {
    return clientKnownOres.chunkCountForLevel(levelIdentity);
}

void clearClientKnownOres() {
    clientKnownOres.clear();
}

} // namespace dobby
