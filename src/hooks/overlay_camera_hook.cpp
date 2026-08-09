#include "hooks/overlay_camera_hook.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "core/runtime_state.hpp"
#include "hooks/minecraft_image.hpp"
#include "hooks/network_metrics_hook.hpp"
#include "hooks/render_camera.hpp"
#include "platform/launcher.hpp"
#include "platform/log.hpp"
#include "ui/entity_hitbox_overlay.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

extern "C" void* dobby_level_render_continue = nullptr;

namespace dobby {
namespace {

std::atomic_bool installed{false};
std::atomic_bool firstEntryLogged{false};
std::atomic_bool firstFrameLogged{false};
std::atomic_bool cameraFailureLogged{false};
std::atomic<const void*> observedLevel{nullptr};

} // namespace

extern "C" void dobby_capture_level_render_camera(
        const void* renderContext) {
    const bool espEnabled = runtimeState().anyEspEnabled();
    const bool metricsEnabled = runtimeState().networkMetricsOverlay();
    if (!espEnabled && !metricsEnabled)
        return;

    if (!firstEntryLogged.exchange(true, std::memory_order_acq_rel))
        logLine("developer overlay: inline level render callback observed");

    const void* level = observedLevel.load(std::memory_order_acquire);
    if (level == nullptr)
        return;

    if (metricsEnabled)
        captureClientServerTick(level);
    if (!espEnabled)
        return;

    CameraFrame camera{};
    if (!captureRenderCameraFrame(renderContext, camera)) {
        if (!cameraFailureLogged.exchange(true, std::memory_order_acq_rel)) {
            logLine("ERROR: ESP rejected the live level render camera context");
        }
        return;
    }
    if (submitOverlayCameraFrame(level, camera) &&
        !firstFrameLogged.exchange(true, std::memory_order_acq_rel)) {
        logLine("ESP: per-frame level render camera capture active");
    }
}

} // namespace dobby

extern "C" [[gnu::naked]] void dobby_level_render_detour() {
    asm volatile(
            // Preserve all integer arguments and the link register while the
            // passive camera snapshot is copied from x1.
            "sub sp, sp, #96\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "str x8, [sp, #64]\n"
            "str x30, [sp, #80]\n"
            "mov x0, x1\n"
            "bl dobby_capture_level_render_camera\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldr x8, [sp, #64]\n"
            "ldr x30, [sp, #80]\n"
            "add sp, sp, #96\n"
            // Replay the exact four validated LevelRendererPlayer::render
            // instructions before continuing at the first untouched byte.
            "sub sp, sp, #160\n"
            "stp x29, x30, [sp, #96]\n"
            "stp x24, x23, [sp, #112]\n"
            "stp x22, x21, [sp, #128]\n"
            "adrp x16, dobby_level_render_continue\n"
            "ldr x16, [x16, :lo12:dobby_level_render_continue]\n"
            "br x16\n");
}

namespace dobby {

void installOverlayCameraHook() {
    if (installed.load(std::memory_order_acquire))
        return;

    const MinecraftImage image = findMinecraftImage();
    const auto functionAddress =
            image.base + target::kLevelRenderFrameOffset;
    const auto slotAddress =
            image.base + target::kLevelRenderFrameVtableSlotOffset;
    const bool targetValid = image.base != 0 &&
            mcpelauncher_patch != nullptr &&
            addressIsExecutable(image, functionAddress) &&
            addressIsInImage(image, slotAddress) &&
            matchesSignature(
                    reinterpret_cast<const void*>(functionAddress),
                    target::kLevelRenderFrameSignature) &&
            configureRenderCameraCapture(image);
    if (!targetValid) {
        logLine("ERROR: ESP frame camera unavailable; level renderer target mismatch");
        return;
    }

    std::uintptr_t currentTarget{};
    std::memcpy(
            &currentTarget, reinterpret_cast<const void*>(slotAddress),
            sizeof(currentTarget));
    if (currentTarget != functionAddress) {
        logLine("ERROR: ESP frame camera unavailable; level renderer vtable mismatch");
        return;
    }

    std::array<std::uint8_t, 16> replacement{};
    constexpr std::uint32_t loadTarget = 0x58000050U;
    constexpr std::uint32_t branchTarget = 0xd61f0200U;
    const auto detour = reinterpret_cast<std::uintptr_t>(
            dobby_level_render_detour);
    std::memcpy(replacement.data(), &loadTarget, sizeof(loadTarget));
    std::memcpy(
            replacement.data() + 4, &branchTarget, sizeof(branchTarget));
    std::memcpy(replacement.data() + 8, &detour, sizeof(detour));

    dobby_level_render_continue = reinterpret_cast<void*>(
            functionAddress + replacement.size());
    auto* entry = reinterpret_cast<void*>(functionAddress);
    if (mcpelauncher_patch(
                entry, replacement.data(), replacement.size()) == nullptr ||
        std::memcmp(entry, replacement.data(), replacement.size()) != 0) {
        dobby_level_render_continue = nullptr;
        logLine("ERROR: ESP frame camera unavailable; inline render patch rejected");
        return;
    }

    installed.store(true, std::memory_order_release);
    logLine("ESP: inline per-frame level render camera hook ready");
}

bool overlayCameraHookInstalled() {
    return installed.load(std::memory_order_acquire);
}

void rememberOverlayLevelIdentity(const void* levelIdentity) {
    if (levelIdentity != nullptr)
        observedLevel.store(levelIdentity, std::memory_order_release);
}

} // namespace dobby

#endif
