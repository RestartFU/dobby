#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dobby {

struct Vec3f {
    float x{};
    float y{};
    float z{};
};

struct EntityAabb {
    Vec3f minimum;
    Vec3f maximum;
};

struct CameraFrame {
    Vec3f position;
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
};

struct ScreenPoint {
    float x{};
    float y{};
};

enum class HitboxSubmissionResult {
    accepted,
    invalidBounds,
    invalidCamera,
    frameCapacityReached,
};

struct EntityHitboxObservation {
    const void* identity{};
    EntityAabb bounds;
};

struct HitboxFrameSubmission {
    std::size_t accepted{};
    std::size_t invalid{};
    std::size_t capacityRejected{};
};

inline constexpr std::uint64_t kMaximumMissedEntityPresentationFrames = 8;

bool validCameraFrame(const CameraFrame& camera);
bool validEntityBounds(const EntityAabb& bounds);
bool projectWorldPoint(
        const CameraFrame& camera, const Vec3f& world, float width, float height,
        ScreenPoint& output);
bool projectWorldSegment(
        const CameraFrame& camera, const Vec3f& firstWorld,
        const Vec3f& secondWorld, float width, float height,
        ScreenPoint& firstOutput, ScreenPoint& secondOutput,
        bool& nearPlaneClipped);
bool worldPointWithinViewport(
        const CameraFrame& camera, const Vec3f& world,
        float normalizedMargin = 1.1F);
bool shouldUseCompactEspMarker(float widthPixels, float heightPixels);
// Entity capture owns bounds only. The level-render hook is the sole camera
// producer so secondary actor render passes cannot replace the world view.
HitboxFrameSubmission submitEntityHitboxFrame(
        const void* levelIdentity,
        std::span<const EntityHitboxObservation> observations);
bool submitOverlayCameraFrame(
        const void* levelIdentity, const CameraFrame& camera);
bool currentOverlayCamera(
        std::uint64_t presentationFrame, const void*& levelIdentity,
        CameraFrame& camera, std::uint64_t* missedFrames = nullptr);
bool entityHitboxObservedForPresentation(
        std::uint64_t presentationFrame, std::uint64_t lastSeenFrame);
std::uint64_t entityHitboxPresentationFrame();
bool installEntityHitboxOverlay();

} // namespace dobby
