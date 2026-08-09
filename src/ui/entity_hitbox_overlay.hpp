#pragma once

#include <array>
#include <cstdint>

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

bool projectWorldPoint(
        const CameraFrame& camera, const Vec3f& world, float width, float height,
        ScreenPoint& output);
HitboxSubmissionResult submitEntityHitbox(
        const void* entityIdentity, const void* levelIdentity,
        const EntityAabb& bounds, const CameraFrame& camera);
bool entityHitboxObservedForPresentation(
        std::uint64_t presentationFrame, std::uint64_t lastSeenFrame);
std::uint64_t entityHitboxPresentationFrame();
bool installEntityHitboxOverlay();

} // namespace dobby
