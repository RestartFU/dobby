#pragma once

#include "hooks/minecraft_image.hpp"
#include "ui/entity_hitbox_overlay.hpp"

namespace dobby {

bool configureRenderCameraCapture(const MinecraftImage& image);
bool captureRenderCameraFrame(
        const void* renderContext, CameraFrame& output);

} // namespace dobby
