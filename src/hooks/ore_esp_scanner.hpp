#pragma once

#include "hooks/minecraft_image.hpp"
#include "ui/ore_esp.hpp"

#include <cstdint>
#include <optional>

namespace dobby {

bool initializeOreEspScanner(const MinecraftImage& image);
bool oreEspScannerReady();
void trackClientChunkForOreEsp(
        const void* chunk, const void* levelIdentity, ChunkPosition position);
void dirtyClientChunkForOreEsp(
        const void* chunk, const void* levelIdentity, ChunkPosition position);
void untrackClientChunkForOreEsp(
        const void* chunk, const void* levelIdentity, ChunkPosition position);
std::optional<OreChunkScanTarget> untrackClientChunkForOreEsp(
        const void* chunk);
void requestClientOreRescan();
void processClientOreRescan(
        const void* levelIdentity, Vec3f cameraPosition);

} // namespace dobby
