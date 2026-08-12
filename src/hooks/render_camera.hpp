#pragma once

#include "hooks/minecraft_image.hpp"
#include "ui/entity_hitbox_overlay.hpp"

#include <cstdint>
#include <string_view>

namespace dobby {

enum class RenderCameraCaptureFailure : std::uint8_t {
    none,
    notConfigured,
    levelRendererUnavailable,
    levelUnavailable,
    renderContextUnavailable,
    cameraUnavailable,
    cameraPositionUnavailable,
    viewMatrixUnavailable,
    projectionMatrixUnavailable,
    invalidFrame,
    count,
};

constexpr std::string_view renderCameraCaptureFailureName(
        RenderCameraCaptureFailure failure) {
    switch (failure) {
    case RenderCameraCaptureFailure::none:
        return "none";
    case RenderCameraCaptureFailure::notConfigured:
        return "not_configured";
    case RenderCameraCaptureFailure::levelRendererUnavailable:
        return "level_renderer";
    case RenderCameraCaptureFailure::levelUnavailable:
        return "client_level";
    case RenderCameraCaptureFailure::renderContextUnavailable:
        return "level_render_context";
    case RenderCameraCaptureFailure::cameraUnavailable:
        return "level_render_camera";
    case RenderCameraCaptureFailure::cameraPositionUnavailable:
        return "camera_position";
    case RenderCameraCaptureFailure::viewMatrixUnavailable:
        return "view_matrix";
    case RenderCameraCaptureFailure::projectionMatrixUnavailable:
        return "projection_matrix";
    case RenderCameraCaptureFailure::invalidFrame:
        return "frame_validation";
    case RenderCameraCaptureFailure::count:
        return "unknown";
    }
    return "unknown";
}

bool configureRenderCameraCapture(const MinecraftImage& image);
// Actor-render capture path. Its BaseActorRenderContext camera-state pointer is
// valid by the time ActorRenderDispatcher invokes the hook.
bool captureRenderCameraFrame(
        const void* renderContext, CameraFrame& output);
// Level-render path. Matrices come from the refreshed render-context Camera;
// position comes from the renderer's target-proven world-camera Vec3.
bool captureLevelRenderCameraFrame(
        const void* levelRenderer, const void* renderContext,
        const void*& levelIdentity, CameraFrame& output,
        RenderCameraCaptureFailure& failure);

} // namespace dobby
