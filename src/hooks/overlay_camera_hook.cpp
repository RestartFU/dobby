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
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>

extern "C" void* dobby_level_render_continue = nullptr;

namespace dobby {
namespace {

std::atomic_bool installed{false};
std::atomic_bool firstEntryLogged{false};
std::atomic_bool firstFrameLogged{false};
std::atomic_uint64_t callbackEntries{};
std::atomic_uint64_t lastWorldRenderMilliseconds{};
std::atomic_uint64_t captureAttempts{};
std::atomic_uint64_t captureSuccesses{};
std::atomic_uint64_t captureFailures{};
constexpr std::size_t kCaptureFailureCount =
        static_cast<std::size_t>(RenderCameraCaptureFailure::count);
static_assert(kCaptureFailureCount <= 32);
std::array<std::atomic_uint64_t, kCaptureFailureCount> failureCounts{};
std::atomic_uint32_t loggedFailureStages{};
std::atomic<RenderCameraCaptureFailure> lastFailure{
        RenderCameraCaptureFailure::none};

std::uint64_t monotonicMilliseconds() {
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
}

void maybeLogCaptureSummary(std::uint64_t entries) {
    constexpr std::uint64_t interval = 600;
    if (entries == 0 || entries % interval != 0)
        return;
    const auto failure = lastFailure.load(std::memory_order_relaxed);
    const std::string_view stage = renderCameraCaptureFailureName(failure);
    char message[256]{};
    std::snprintf(
            message, sizeof(message),
            "ESP camera pipeline: entries=%llu attempts=%llu successes=%llu "
            "failures=%llu last_failure=%.*s",
            static_cast<unsigned long long>(entries),
            static_cast<unsigned long long>(
                    captureAttempts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                    captureSuccesses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                    captureFailures.load(std::memory_order_relaxed)),
            static_cast<int>(stage.size()), stage.data());
    logLine(message);
}

void recordCaptureFailure(RenderCameraCaptureFailure failure) {
    std::size_t index = static_cast<std::size_t>(failure);
    if (failure == RenderCameraCaptureFailure::none ||
        index >= failureCounts.size()) {
        failure = RenderCameraCaptureFailure::invalidFrame;
        index = static_cast<std::size_t>(failure);
    }
    const std::uint64_t stageCount =
            failureCounts[index].fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint64_t totalFailures =
            captureFailures.fetch_add(1, std::memory_order_relaxed) + 1;
    lastFailure.store(failure, std::memory_order_relaxed);
    const std::uint32_t bit = std::uint32_t{1} << index;
    if ((loggedFailureStages.fetch_or(bit, std::memory_order_acq_rel) & bit) !=
        0) {
        return;
    }
    const std::string_view stage = renderCameraCaptureFailureName(failure);
    char message[256]{};
    std::snprintf(
            message, sizeof(message),
            "ERROR: ESP level-render camera capture failed at %.*s "
            "(attempts=%llu successes=%llu failures=%llu stage_count=%llu)",
            static_cast<int>(stage.size()), stage.data(),
            static_cast<unsigned long long>(
                    captureAttempts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                    captureSuccesses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(totalFailures),
            static_cast<unsigned long long>(stageCount));
    logLine(message);
}

} // namespace

extern "C" void dobby_capture_level_render_camera(
        const void* levelRenderer, const void* renderContext) {
    lastWorldRenderMilliseconds.store(
            monotonicMilliseconds(), std::memory_order_release);
    const bool espEnabled = runtimeState().anyEspEnabled();
    const bool metricsEnabled = runtimeState().networkMetricsOverlay();
    if (!espEnabled && !metricsEnabled)
        return;

    const std::uint64_t entries =
            callbackEntries.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!firstEntryLogged.exchange(true, std::memory_order_acq_rel)) {
        char message[128]{};
        std::snprintf(
                message, sizeof(message),
                "developer overlay: post-update camera callback observed "
                "(entries=%llu)",
                static_cast<unsigned long long>(entries));
        logLine(message);
    }

    if (metricsEnabled)
        captureObservedClientServerTick();
    if (!espEnabled)
        return;

    captureAttempts.fetch_add(1, std::memory_order_relaxed);
    const void* level = nullptr;
    CameraFrame camera{};
    RenderCameraCaptureFailure failure = RenderCameraCaptureFailure::none;
    if (!captureLevelRenderCameraFrame(
                levelRenderer, renderContext, level, camera, failure)) {
        recordCaptureFailure(failure);
        maybeLogCaptureSummary(entries);
        return;
    }
    lastFailure.store(RenderCameraCaptureFailure::none,
                      std::memory_order_relaxed);
    const std::uint64_t successes =
            captureSuccesses.fetch_add(1, std::memory_order_relaxed) + 1;
    if (submitOverlayCameraFrame(level, camera) &&
        !firstFrameLogged.exchange(true, std::memory_order_acq_rel)) {
        char message[192]{};
        std::snprintf(
                message, sizeof(message),
                "ESP: direct level-render camera capture active "
                "(attempts=%llu successes=%llu failures=%llu)",
                static_cast<unsigned long long>(
                        captureAttempts.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(successes),
                static_cast<unsigned long long>(
                        captureFailures.load(std::memory_order_relaxed)));
        logLine(message);
    }
    maybeLogCaptureSummary(entries);
}

} // namespace dobby

extern "C" [[gnu::naked]] void dobby_level_render_detour() {
    asm volatile(
            // Bedrock has just refreshed [x22 + 0x18]. Preserve caller-saved
            // integer state around the passive snapshot; x18 is Android's
            // platform register and is deliberately left untouched.
            "sub sp, sp, #160\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "stp x8, x9, [sp, #64]\n"
            "stp x10, x11, [sp, #80]\n"
            "stp x12, x13, [sp, #96]\n"
            "stp x14, x15, [sp, #112]\n"
            "stp x16, x17, [sp, #128]\n"
            "str x30, [sp, #144]\n"
            "mov x0, x19\n"
            "mov x1, x22\n"
            "bl dobby_capture_level_render_camera\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldp x8, x9, [sp, #64]\n"
            "ldp x10, x11, [sp, #80]\n"
            "ldp x12, x13, [sp, #96]\n"
            "ldp x14, x15, [sp, #112]\n"
            "ldp x16, x17, [sp, #128]\n"
            "ldr x30, [sp, #144]\n"
            "add sp, sp, #160\n"
            // Replay the exact four instructions replaced at 0xAE1A1C4.
            "ldr x8, [x19, #2432]\n"
            "ldr x24, [x22, #24]\n"
            "add x20, x19, #1800\n"
            "ldr x23, [x8, #968]\n"
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
    const auto captureAddress =
            image.base + target::kLevelRenderCameraCaptureOffset;
    const auto slotAddress =
            image.base + target::kLevelRenderFrameVtableSlotOffset;
    const bool targetValid = image.base != 0 &&
            mcpelauncher_patch != nullptr &&
            addressIsExecutable(image, functionAddress) &&
            addressIsExecutable(image, captureAddress) &&
            addressIsInImage(image, slotAddress) &&
            matchesSignature(
                    reinterpret_cast<const void*>(functionAddress),
                    target::kLevelRenderFrameSignature) &&
            matchesSignature(
                    reinterpret_cast<const void*>(captureAddress),
                    target::kLevelRenderCameraCaptureSignature) &&
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
            captureAddress + replacement.size());
    auto* entry = reinterpret_cast<void*>(captureAddress);
    if (mcpelauncher_patch(
                entry, replacement.data(), replacement.size()) == nullptr ||
        std::memcmp(entry, replacement.data(), replacement.size()) != 0) {
        dobby_level_render_continue = nullptr;
        logLine("ERROR: ESP frame camera unavailable; inline render patch rejected");
        return;
    }

    installed.store(true, std::memory_order_release);
    logLine("ESP: post-update per-frame level render camera hook ready");
}

bool overlayCameraHookInstalled() {
    return installed.load(std::memory_order_acquire);
}

bool clientWorldRecentlyRendered() {
    return worldRenderIsFresh(
            lastWorldRenderMilliseconds.load(std::memory_order_acquire),
            monotonicMilliseconds());
}

} // namespace dobby

#endif
