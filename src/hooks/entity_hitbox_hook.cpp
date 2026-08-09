#include "hooks/entity_hitbox_hook.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "core/runtime_state.hpp"
#include "hooks/minecraft_image.hpp"
#include "platform/launcher.hpp"
#include "platform/log.hpp"
#include "ui/entity_hitbox_overlay.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

extern "C" void* dobby_actor_render_continue = nullptr;

namespace dobby {
namespace {

using ActorGetAabbFn = const EntityAabb* (*)(const void* actor);
using ContextGetCameraPositionFn = const Vec3f* (*)(const void* renderContext);
using LevelGetRuntimeActorListFn = std::vector<const void*> (*)(const void* level);
struct OpaquePlayer;
using PlayerVisitor = std::function<bool(OpaquePlayer&)>;
using LevelForEachPlayerFn = void (*)(void* level, PlayerVisitor visitor);
using LevelGetPrimaryLocalPlayerFn = const void* (*)(const void* level);

std::atomic_bool hookInstalled{false};
std::atomic_bool acceptedCaptureLogged{false};
std::atomic_bool invalidBoundsLogged{false};
std::atomic_bool invalidCameraLogged{false};
std::atomic_bool capacityLogged{false};
std::atomic_bool registryCaptureLogged{false};
std::atomic_bool playerCaptureLogged{false};
std::atomic_bool registryLayoutRejectedLogged{false};
std::atomic_bool registryCapacityLogged{false};
std::atomic_bool playerCapacityLogged{false};
ActorGetAabbFn actorGetAabb = nullptr;
ContextGetCameraPositionFn contextGetCameraPosition = nullptr;
LevelGetRuntimeActorListFn levelGetRuntimeActorList = nullptr;
LevelForEachPlayerFn levelForEachPlayer = nullptr;
LevelGetPrimaryLocalPlayerFn levelGetPrimaryLocalPlayer = nullptr;
std::uintptr_t expectedClientLevelVtable{};
MinecraftImage minecraftImage{};
std::atomic_uint64_t lastRegistryCaptureFrame{std::numeric_limits<std::uint64_t>::max()};
std::atomic_uint64_t lastPlayerCaptureFrame{std::numeric_limits<std::uint64_t>::max()};

template <class Value>
Value readObjectField(const void* object, std::ptrdiff_t offset) {
    Value value{};
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}

bool readMatrixStackTop(
        const void* stackObject, std::array<float, 16>& output) {
    if (stackObject == nullptr)
        return false;
    const auto* stack = static_cast<const std::byte*>(stackObject);
    const auto* mapBegin = readObjectField<const std::byte*>(
            stack, target::kMatrixStackMapBeginOffset);
    const auto* mapEnd = readObjectField<const std::byte*>(
            stack, target::kMatrixStackMapEndOffset);
    const std::size_t start = readObjectField<std::size_t>(
            stack, target::kMatrixStackStartOffset);
    const std::size_t size = readObjectField<std::size_t>(
            stack, target::kMatrixStackSizeOffset);
    const auto mapBeginAddress = reinterpret_cast<std::uintptr_t>(mapBegin);
    const auto mapEndAddress = reinterpret_cast<std::uintptr_t>(mapEnd);
    if (mapBegin == nullptr || mapEnd == nullptr || mapEndAddress <= mapBeginAddress ||
        (mapEndAddress - mapBeginAddress) % sizeof(void*) != 0 ||
        mapEndAddress - mapBeginAddress > 128 * sizeof(void*) || size == 0 ||
        size > 128 || start > std::numeric_limits<std::size_t>::max() - size) {
        return false;
    }

    const std::size_t element = start + size - 1;
    const std::size_t mapIndex = element / target::kMatricesPerDequeBlock;
    const std::size_t mapSize = (mapEndAddress - mapBeginAddress) / sizeof(void*);
    if (mapIndex >= mapSize)
        return false;
    const auto* block = readObjectField<const std::byte*>(
            mapBegin, static_cast<std::ptrdiff_t>(mapIndex * sizeof(void*)));
    if (block == nullptr)
        return false;
    const auto* matrix = reinterpret_cast<const float*>(
            block + (element % target::kMatricesPerDequeBlock) * target::kMatrixBytes);
    std::memcpy(output.data(), matrix, target::kMatrixBytes);
    for (const float value : output) {
        if (!std::isfinite(value) || std::fabs(value) >= 1.0e8F)
            return false;
    }
    return true;
}

bool validClientLevel(const void* level) {
    if (level == nullptr || expectedClientLevelVtable == 0 ||
        levelGetRuntimeActorList == nullptr || levelForEachPlayer == nullptr ||
        levelGetPrimaryLocalPlayer == nullptr) {
        return false;
    }
    const auto levelAddress = reinterpret_cast<std::uintptr_t>(level);
    if (levelAddress % alignof(void*) != 0)
        return false;
    const auto vtable = readObjectField<std::uintptr_t>(level, 0);
    constexpr std::size_t largestSlot =
            target::kLevelGetRuntimeActorListVtableSlot;
    const auto vtableEnd = vtable + (largestSlot + 1) * sizeof(std::uintptr_t);
    if (vtable != expectedClientLevelVtable || vtableEnd < vtable ||
        !addressIsInImage(minecraftImage, vtable) ||
        vtableEnd > minecraftImage.loadEnd) {
        if (!registryLayoutRejectedLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: entity registry unavailable; ClientLevel layout mismatch");
        return false;
    }

    const auto* slots = reinterpret_cast<const std::uintptr_t*>(vtable);
    const auto actorListTarget = reinterpret_cast<std::uintptr_t>(
            levelGetRuntimeActorList);
    const auto playerListTarget = reinterpret_cast<std::uintptr_t>(
            levelForEachPlayer);
    const auto primaryPlayerTarget = reinterpret_cast<std::uintptr_t>(
            levelGetPrimaryLocalPlayer);
    if (slots[target::kLevelGetRuntimeActorListVtableSlot] != actorListTarget ||
        slots[target::kLevelForEachPlayerVtableSlot] != playerListTarget ||
        slots[target::kLevelGetPrimaryLocalPlayerVtableSlot] !=
                primaryPlayerTarget) {
        if (!registryLayoutRejectedLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: entity registry unavailable; ClientLevel vtable mismatch");
        return false;
    }
    return true;
}

bool captureRuntimeActorList(
        const void* level, const CameraFrame& frame) {
    if (!validClientLevel(level))
        return false;

    const std::uint64_t frameNumber = entityHitboxPresentationFrame();
    std::uint64_t previous = lastRegistryCaptureFrame.load(std::memory_order_acquire);
    while (previous != frameNumber) {
        if (lastRegistryCaptureFrame.compare_exchange_weak(
                    previous, frameNumber, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
            const auto actors = levelGetRuntimeActorList(level);
            const std::size_t count =
                    std::min(actors.size(), std::size_t{512});
            const void* localPlayer = levelGetPrimaryLocalPlayer(level);
            std::size_t captured = 0;
            for (std::size_t index = 0; index < count; ++index) {
                const void* actor = actors[index];
                if (actor == nullptr || actor == localPlayer)
                    continue;
                const auto* bounds = actorGetAabb(actor);
                if (bounds != nullptr && submitEntityHitbox(
                            actor, level, *bounds, frame) ==
                            HitboxSubmissionResult::accepted) {
                    ++captured;
                }
            }
            if (actors.size() > count &&
                !registryCapacityLogged.exchange(true, std::memory_order_acq_rel)) {
                logLine("entity registry: capture capped at 512 actors");
            }
            if (captured != 0 &&
                !registryCaptureLogged.exchange(true, std::memory_order_acq_rel)) {
                char message[128]{};
                std::snprintf(message, sizeof(message),
                              "entity registry: captured %zu of %zu active actors",
                              captured, actors.size());
                logLine(message);
            }
            return true;
        }
    }
    return true;
}

bool captureRuntimePlayers(const void* level, const CameraFrame& frame) {
    if (!validClientLevel(level))
        return false;

    const std::uint64_t frameNumber = entityHitboxPresentationFrame();
    std::uint64_t previous = lastPlayerCaptureFrame.load(std::memory_order_acquire);
    while (previous != frameNumber) {
        if (lastPlayerCaptureFrame.compare_exchange_weak(
                    previous, frameNumber, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
            constexpr std::size_t maximumPlayers = 256;
            std::size_t visited = 0;
            std::size_t captured = 0;
            const void* localPlayer = levelGetPrimaryLocalPlayer(level);
            PlayerVisitor visitor = [&](OpaquePlayer& player) {
                if (visited >= maximumPlayers)
                    return false;
                ++visited;
                const void* actor = static_cast<const void*>(&player);
                if (actor == localPlayer)
                    return visited < maximumPlayers;
                const auto* bounds = actorGetAabb(actor);
                if (bounds != nullptr && submitEntityHitbox(
                            actor, level, *bounds, frame) ==
                            HitboxSubmissionResult::accepted) {
                    ++captured;
                }
                return visited < maximumPlayers;
            };
            levelForEachPlayer(const_cast<void*>(level), std::move(visitor));
            if (visited == maximumPlayers &&
                !playerCapacityLogged.exchange(true, std::memory_order_acq_rel)) {
                logLine("player registry: capture capped at 256 players");
            }
            if (visited != 0 &&
                !playerCaptureLogged.exchange(true, std::memory_order_acq_rel)) {
                char message[128]{};
                std::snprintf(message, sizeof(message),
                              "player registry: captured %zu of %zu active players",
                              captured, visited);
                logLine(message);
            }
            return true;
        }
    }
    return true;
}

} // namespace

extern "C" void dobby_capture_entity_hitbox(
        const void* renderContext, const void* actor) {
    if (!runtimeState().entityHitboxes() || renderContext == nullptr || actor == nullptr ||
        actorGetAabb == nullptr || contextGetCameraPosition == nullptr) {
        return;
    }

    const auto* bounds = actorGetAabb(actor);
    const void* screenContext = readObjectField<const void*>(
            renderContext, target::kRenderContextScreenContextOffset);
    if (bounds == nullptr || screenContext == nullptr)
        return;
    const void* camera = readObjectField<const void*>(
            screenContext, target::kScreenContextCameraOffset);
    if (camera == nullptr)
        return;

    CameraFrame frame{};
    const Vec3f* cameraPosition = contextGetCameraPosition(renderContext);
    if (cameraPosition == nullptr)
        return;
    frame.position = *cameraPosition;
    const bool viewRead = readMatrixStackTop(camera, frame.view);
    const bool projectionRead = readMatrixStackTop(
            static_cast<const std::byte*>(camera) +
                    target::kCameraProjectionStackOffset,
            frame.projection);
    if (!viewRead || !projectionRead)
        return;
    const void* level = readObjectField<const void*>(actor, target::kActorLevelOffset);
    const bool localPlayer = validClientLevel(level) &&
            actor == levelGetPrimaryLocalPlayer(level);
    const auto result = localPlayer
            ? HitboxSubmissionResult::accepted
            : submitEntityHitbox(actor, level, *bounds, frame);
    static_cast<void>(captureRuntimeActorList(level, frame));
    static_cast<void>(captureRuntimePlayers(level, frame));
    if (localPlayer)
        return;
    std::atomic_bool* sampleFlag = nullptr;
    const char* resultName = "accepted";
    switch (result) {
    case HitboxSubmissionResult::accepted:
        sampleFlag = &acceptedCaptureLogged;
        break;
    case HitboxSubmissionResult::invalidBounds:
        sampleFlag = &invalidBoundsLogged;
        resultName = "invalid_bounds";
        break;
    case HitboxSubmissionResult::invalidCamera:
        sampleFlag = &invalidCameraLogged;
        resultName = "invalid_camera";
        break;
    case HitboxSubmissionResult::frameCapacityReached:
        sampleFlag = &capacityLogged;
        resultName = "frame_capacity";
        break;
    }
    if (sampleFlag != nullptr && !sampleFlag->exchange(true, std::memory_order_acq_rel)) {
        char message[768]{};
        std::snprintf(
                message, sizeof(message),
                "entity capture sample: result=%s bounds=(%.3f,%.3f,%.3f)->"
                "(%.3f,%.3f,%.3f) camera=(%.3f,%.3f,%.3f) "
                "view_diag=(%.3f,%.3f,%.3f,%.3f) "
                "projection_diag=(%.3f,%.3f,%.3f,%.3f)",
                resultName,
                bounds->minimum.x, bounds->minimum.y, bounds->minimum.z,
                bounds->maximum.x, bounds->maximum.y, bounds->maximum.z,
                frame.position.x, frame.position.y, frame.position.z,
                frame.view[0], frame.view[5], frame.view[10], frame.view[15],
                frame.projection[0], frame.projection[5],
                frame.projection[10], frame.projection[15]);
        logLine(message);
    }
}

} // namespace dobby

extern "C" [[gnu::naked]] void dobby_actor_render_detour() {
    asm volatile(
            // Capture the context and actor before Bedrock's render prologue.
            // x0-x8 and x30 are restored because the capture is transparent.
            "sub sp, sp, #96\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "str x8, [sp, #64]\n"
            "str x30, [sp, #80]\n"
            "mov x0, x1\n"
            "mov x1, x2\n"
            "bl dobby_capture_entity_hitbox\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldr x8, [sp, #64]\n"
            "ldr x30, [sp, #80]\n"
            "add sp, sp, #96\n"
            // Replay the four validated ActorRenderDispatcher instructions.
            "sub sp, sp, #144\n"
            "stp d11, d10, [sp, #48]\n"
            "stp d9, d8, [sp, #64]\n"
            "stp x29, x30, [sp, #80]\n"
            "adrp x16, dobby_actor_render_continue\n"
            "ldr x16, [x16, :lo12:dobby_actor_render_continue]\n"
            "br x16\n");
}

namespace dobby {

void installEntityHitboxHook() {
    if (hookInstalled.load(std::memory_order_acquire))
        return;

    const auto image = findMinecraftImage();
    const auto renderAddress = image.base + target::kActorRenderOffset;
    const auto getAabbAddress = image.base + target::kActorGetAabbOffset;
    const auto getLevelAddress = image.base + target::kActorGetLevelOffset;
    const auto runtimeActorListAddress =
            image.base + target::kLevelGetRuntimeActorListOffset;
    const auto forEachPlayerAddress =
            image.base + target::kLevelForEachPlayerOffset;
    const auto primaryLocalPlayerAddress =
            image.base + target::kLevelGetPrimaryLocalPlayerOffset;
    const auto projectionGetterAddress = image.base + target::kProjectionMatrixGetterOffset;
    const auto viewGetterAddress = image.base + target::kViewMatrixGetterOffset;
    const auto cameraPositionGetterAddress = image.base + target::kCameraPositionGetterOffset;
    const bool valid = image.base != 0 &&
            addressIsExecutable(image, renderAddress) &&
            addressIsExecutable(image, getAabbAddress) &&
            addressIsExecutable(image, getLevelAddress) &&
            addressIsExecutable(image, runtimeActorListAddress) &&
            addressIsExecutable(image, forEachPlayerAddress) &&
            addressIsExecutable(image, primaryLocalPlayerAddress) &&
            addressIsExecutable(image, projectionGetterAddress) &&
            addressIsExecutable(image, viewGetterAddress) &&
            addressIsExecutable(image, cameraPositionGetterAddress) &&
            matchesSignature(reinterpret_cast<const void*>(renderAddress),
                             target::kActorRenderSignature) &&
            matchesSignature(reinterpret_cast<const void*>(getAabbAddress),
                             target::kActorGetAabbSignature) &&
            matchesSignature(reinterpret_cast<const void*>(getLevelAddress),
                             target::kActorGetLevelSignature) &&
            matchesSignature(reinterpret_cast<const void*>(runtimeActorListAddress),
                             target::kLevelGetRuntimeActorListSignature) &&
            matchesSignature(reinterpret_cast<const void*>(forEachPlayerAddress),
                             target::kLevelForEachPlayerSignature) &&
            matchesSignature(reinterpret_cast<const void*>(primaryLocalPlayerAddress),
                             target::kLevelGetPrimaryLocalPlayerSignature) &&
            matchesSignature(reinterpret_cast<const void*>(projectionGetterAddress),
                             target::kProjectionMatrixGetterSignature) &&
            matchesSignature(reinterpret_cast<const void*>(viewGetterAddress),
                             target::kViewMatrixGetterSignature) &&
            matchesSignature(reinterpret_cast<const void*>(cameraPositionGetterAddress),
                             target::kCameraPositionGetterSignature);
    if (!valid) {
        runtimeState().setEntityHitboxesAvailable(false);
        logLine("ERROR: entity overlay unavailable; Bedrock render layout mismatch");
        return;
    }
    if (mcpelauncher_patch == nullptr) {
        runtimeState().setEntityHitboxesAvailable(false);
        logLine("ERROR: entity overlay unavailable; launcher patch API missing");
        return;
    }

    std::array<std::uint8_t, 16> replacement{};
    constexpr std::uint32_t loadTarget = 0x58000050U;
    constexpr std::uint32_t branchTarget = 0xd61f0200U;
    const auto detour = reinterpret_cast<std::uintptr_t>(dobby_actor_render_detour);
    std::memcpy(replacement.data(), &loadTarget, sizeof(loadTarget));
    std::memcpy(replacement.data() + 4, &branchTarget, sizeof(branchTarget));
    std::memcpy(replacement.data() + 8, &detour, sizeof(detour));

    actorGetAabb = reinterpret_cast<ActorGetAabbFn>(getAabbAddress);
    levelGetRuntimeActorList = reinterpret_cast<LevelGetRuntimeActorListFn>(
            runtimeActorListAddress);
    levelForEachPlayer = reinterpret_cast<LevelForEachPlayerFn>(
            forEachPlayerAddress);
    levelGetPrimaryLocalPlayer = reinterpret_cast<LevelGetPrimaryLocalPlayerFn>(
            primaryLocalPlayerAddress);
    expectedClientLevelVtable = image.base + target::kClientLevelVtableOffset;
    minecraftImage = image;
    contextGetCameraPosition = reinterpret_cast<ContextGetCameraPositionFn>(
            cameraPositionGetterAddress);
    dobby_actor_render_continue = reinterpret_cast<void*>(renderAddress + replacement.size());
    auto* entry = reinterpret_cast<void*>(renderAddress);
    if (mcpelauncher_patch(entry, replacement.data(), replacement.size()) == nullptr ||
        std::memcmp(entry, replacement.data(), replacement.size()) != 0) {
        actorGetAabb = nullptr;
        levelGetRuntimeActorList = nullptr;
        levelForEachPlayer = nullptr;
        levelGetPrimaryLocalPlayer = nullptr;
        expectedClientLevelVtable = 0;
        minecraftImage = {};
        contextGetCameraPosition = nullptr;
        dobby_actor_render_continue = nullptr;
        runtimeState().setEntityHitboxesAvailable(false);
        logLine("ERROR: entity overlay unavailable; actor render hook rejected");
        return;
    }

    hookInstalled.store(true, std::memory_order_release);
    logLine("entity overlay: client actor bounds capture ready");
}

bool entityHitboxHookInstalled() {
    return hookInstalled.load(std::memory_order_acquire);
}

bool toggleEntityHitboxes() {
    if (!runtimeState().entityHitboxesAvailable()) {
        logLine("ERROR: entity hitbox overlay is unavailable");
        return false;
    }
    const bool enabled = !runtimeState().entityHitboxes();
    runtimeState().setEntityHitboxes(enabled);
    logLine(enabled ? "UI: entity hitboxes enabled" : "UI: entity hitboxes disabled");
    return enabled;
}

} // namespace dobby
#endif
