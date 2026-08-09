#include "hooks/packet_hooks.hpp"

#include "core/config.hpp"
#include "core/constants.hpp"
#include "core/runtime_state.hpp"
#include "diagnostics/report_builder.hpp"
#include "diagnostics/stream_probe.hpp"
#include "diagnostics/violation_decoder.hpp"
#include "hooks/minecraft_image.hpp"
#include "platform/files.hpp"
#include "platform/launcher.hpp"
#include "platform/log.hpp"
#include "ui/developer_ui.hpp"

#include <chrono>
#include <cstdint>
#include <string>

#if defined(__ANDROID__)

extern "C" void* dobby_original_stream_read = nullptr;

extern "C" void dobby_capture_read_attempt(const void* stream, std::uint64_t requested) {
    dobby::captureStreamReadAttempt(stream, static_cast<std::size_t>(requested),
                                    dobby::config().rawCaptureLimit);
}

extern "C" [[gnu::naked]] void dobby_stream_read_detour() {
    asm volatile(
            "sub sp, sp, #80\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "str x8, [sp, #64]\n"
            "mov x1, x2\n"
            "bl dobby_capture_read_attempt\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldr x8, [sp, #64]\n"
            "add sp, sp, #80\n"
            "adrp x16, dobby_original_stream_read\n"
            "ldr x16, [x16, :lo12:dobby_original_stream_read]\n"
            "br x16\n");
}

namespace dobby {
namespace {

using GetIdFn = std::int32_t (*)(const void* packet);
GetIdFn originalViolationGetId = nullptr;
thread_local bool handlingViolation = false;

void persistDiagnostic(const Diagnostic& diagnostic) {
    writeFile(latestPath(), diagnostic.report, "w");
    writeFile(eventPath(), diagnostic.json + "\n", "a");
    logLine(diagnostic.json);
}

void handleViolation(const char* intercept, const void* packet) {
    if (handlingViolation)
        return;
    handlingViolation = true;

    const auto record = decodeViolation(packet);
    if (!record) {
        logLine(std::string("ERROR: unable to decode PacketViolationWarningPacket at ") + intercept);
        handlingViolation = false;
        return;
    }

    auto diagnostic = buildDiagnostic(
            *record, recentStreamFailure(std::chrono::seconds(10)), intercept);
    persistDiagnostic(diagnostic);
    runtimeState().addDiagnostic(std::move(diagnostic));

    // Every violation is eligible for a popup. History retention and repeated
    // payloads never suppress a later disconnect after the user closes a window.
    if (runtimeState().autoPopup())
        showLatestViolation();
    handlingViolation = false;
}

std::int32_t violationGetIdDetour(const void* packet) {
    handleViolation("PacketViolationWarningPacket::getId vtable", packet);
    return originalViolationGetId != nullptr ? originalViolationGetId(packet) : 156;
}

bool installStreamProbe(const MinecraftImage& image) {
    const auto functionAddress = image.base + target::kStreamReadOffset;
    if (!addressIsExecutable(image, functionAddress) ||
        !matchesSignature(reinterpret_cast<const void*>(functionAddress), target::kStreamReadSignature)) {
        logLine("ERROR: ReadOnlyBinaryStream::read signature mismatch; raw stream probe disabled");
        return false;
    }

    auto* slot = reinterpret_cast<void**>(image.base + target::kStreamReadVtableSlotOffset);
    if (*slot != reinterpret_cast<void*>(functionAddress)) {
        logLine("ERROR: ReadOnlyBinaryStream::read vtable mismatch; raw stream probe disabled");
        return false;
    }
    if (mcpelauncher_patch == nullptr) {
        logLine("ERROR: launcher patch API unavailable; raw stream probe disabled");
        return false;
    }

    dobby_original_stream_read = *slot;
    void* replacement = reinterpret_cast<void*>(dobby_stream_read_detour);
    if (mcpelauncher_patch(slot, &replacement, sizeof(replacement)) == nullptr || *slot != replacement) {
        dobby_original_stream_read = nullptr;
        logLine("ERROR: launcher rejected ReadOnlyBinaryStream::read probe");
        return false;
    }
    logLine("installed ReadOnlyBinaryStream::read byte-trace probe");
    return true;
}

bool installViolationHook(const MinecraftImage& image) {
    const auto functionAddress = image.base + target::kViolationGetIdOffset;
    if (!addressIsExecutable(image, functionAddress) ||
        !matchesSignature(reinterpret_cast<const void*>(functionAddress), target::kViolationGetIdSignature)) {
        logLine("ERROR: PacketViolationWarningPacket::getId signature mismatch");
        return false;
    }

    auto* slot = reinterpret_cast<void**>(image.base + target::kViolationGetIdVtableSlotOffset);
    if (*slot != reinterpret_cast<void*>(functionAddress)) {
        logLine("ERROR: PacketViolationWarningPacket vtable mismatch");
        return false;
    }
    if (mcpelauncher_patch == nullptr) {
        logLine("ERROR: launcher patch API unavailable");
        return false;
    }

    originalViolationGetId = reinterpret_cast<GetIdFn>(*slot);
    void* replacement = reinterpret_cast<void*>(violationGetIdDetour);
    if (mcpelauncher_patch(slot, &replacement, sizeof(replacement)) == nullptr || *slot != replacement) {
        originalViolationGetId = nullptr;
        logLine("ERROR: launcher rejected PacketViolationWarningPacket hook");
        return false;
    }
    logLine("installed PacketViolationWarningPacket::getId hook");
    return true;
}

} // namespace

void installPacketHooks() {
    const auto image = findMinecraftImage();
    if (image.base == 0 || image.executableBegin == 0) {
        runtimeState().setHookStatus("failed: libminecraftpe.so not found", false, false);
        recordLifecycleEvent("hook_error", "libminecraftpe.so not found");
        logLine("ERROR: libminecraftpe.so not found");
        return;
    }

    const bool streamProbe = installStreamProbe(image);
    const bool warningHook = installViolationHook(image);
    if (!warningHook) {
        runtimeState().setHookStatus("failed: violation hook unavailable", false, streamProbe);
        recordLifecycleEvent("hook_error", "PacketViolationWarningPacket hook unavailable");
        return;
    }

    runtimeState().setHookStatus(
            streamProbe ? "active: violation + byte trace" : "active: violation only",
            true, streamProbe);
    recordLifecycleEvent(
            "hook_ready", streamProbe
                    ? "packet violations and ReadOnlyBinaryStream byte tracing active"
                    : "packet violations active; stream tracing unavailable");
    logLine("READY: Dobby 2.1.0 developer diagnostics active");
}

} // namespace dobby
#endif
