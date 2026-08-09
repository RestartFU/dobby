#pragma once

#include "hooks/minecraft_image.hpp"
#include "ui/chest_esp.hpp"

#include <cstdint>

namespace dobby {

bool initializeOreEspScanner(const MinecraftImage& image);
bool oreEspScannerReady();
void scanClientChunkOres(
        const void* chunk, const void* levelIdentity, ChunkPosition position);
void scanClientSubChunkOres(
        const void* chunk, const void* levelIdentity, ChunkPosition position,
        std::int16_t absoluteSubChunk);

} // namespace dobby
