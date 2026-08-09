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
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(__ANDROID__)

extern "C" void* dobby_stream_read_continue = nullptr;
extern "C" void* dobby_packet_end_continue = nullptr;

extern "C" void dobby_capture_read_attempt(const void* stream, std::uint64_t requested) {
    dobby::captureStreamReadAttempt(stream, static_cast<std::size_t>(requested),
                                    dobby::config().rawCaptureLimit);
}

extern "C" void dobby_capture_packet_end(const void* stream) {
    dobby::capturePacketEndCheck(stream, dobby::config().rawCaptureLimit);
}

extern "C" [[gnu::naked]] void dobby_stream_read_detour() {
    asm volatile(
            // BL replaces x30 with its own return address. Preserve the
            // caller's x30 so the original Bedrock function returns to its
            // actual caller after this detour tail-branches into it.
            "sub sp, sp, #96\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "str x8, [sp, #64]\n"
            "str x30, [sp, #80]\n"
            "mov x1, x2\n"
            "bl dobby_capture_read_attempt\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldr x8, [sp, #64]\n"
            "ldr x30, [sp, #80]\n"
            "add sp, sp, #96\n"
            // Replay the four instructions replaced by the inline entry
            // patch, then continue in the original function. Patching the
            // entry (instead of only its vtable slot) observes direct and
            // virtual primitive reads through the same validated path.
            "sub sp, sp, #288\n"
            "stp x29, x30, [sp, #224]\n"
            "str x28, [sp, #240]\n"
            "stp x22, x21, [sp, #256]\n"
            "adrp x16, dobby_stream_read_continue\n"
            "ldr x16, [x16, :lo12:dobby_stream_read_continue]\n"
            "br x16\n");
}

extern "C" [[gnu::naked]] void dobby_packet_end_detour() {
    asm volatile(
            "sub sp, sp, #96\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "str x8, [sp, #64]\n"
            "str x30, [sp, #80]\n"
            "bl dobby_capture_packet_end\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldr x8, [sp, #64]\n"
            "ldr x30, [sp, #80]\n"
            "add sp, sp, #96\n"
            // Replay the verified ReadOnlyBinaryStream packet-completion
            // prologue before returning to Bedrock after the inline patch.
            "sub sp, sp, #144\n"
            "stp x29, x30, [sp, #96]\n"
            "str x21, [sp, #112]\n"
            "stp x20, x19, [sp, #128]\n"
            "adrp x16, dobby_packet_end_continue\n"
            "ldr x16, [x16, :lo12:dobby_packet_end_continue]\n"
            "br x16\n");
}

namespace dobby {
namespace {

using GetIdFn = std::int32_t (*)(const void* packet);
GetIdFn originalViolationGetId = nullptr;
thread_local bool handlingViolation = false;
thread_local const void* lastViolationPacket = nullptr;
thread_local std::chrono::steady_clock::time_point lastViolationAt{};

void persistDiagnostic(const Diagnostic& diagnostic) {
    writeFile(latestPath(), diagnostic.report, "w");
    writeFile(eventPath(), diagnostic.json + "\n", "a");
    logLine(diagnostic.json);
}

void handleViolation(const char* intercept, const void* packet) {
    if (handlingViolation)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (packet == lastViolationPacket && now - lastViolationAt < std::chrono::seconds(1))
        return;
    lastViolationPacket = packet;
    lastViolationAt = now;
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

    // ldr x16, #8; br x16; .quad detour. The absolute target avoids the
    // AArch64 +/-128 MiB direct-branch limit between libminecraftpe and Dobby.
    std::array<std::uint8_t, 16> replacement{};
    constexpr std::uint32_t loadTarget = 0x58000050U;
    constexpr std::uint32_t branchTarget = 0xd61f0200U;
    const auto detour = reinterpret_cast<std::uintptr_t>(dobby_stream_read_detour);
    std::memcpy(replacement.data(), &loadTarget, sizeof(loadTarget));
    std::memcpy(replacement.data() + 4, &branchTarget, sizeof(branchTarget));
    std::memcpy(replacement.data() + 8, &detour, sizeof(detour));

    dobby_stream_read_continue = reinterpret_cast<void*>(functionAddress + replacement.size());
    auto* entry = reinterpret_cast<void*>(functionAddress);
    if (mcpelauncher_patch(entry, replacement.data(), replacement.size()) == nullptr ||
        std::memcmp(entry, replacement.data(), replacement.size()) != 0) {
        dobby_stream_read_continue = nullptr;
        logLine("ERROR: launcher rejected ReadOnlyBinaryStream::read inline probe");
        return false;
    }
    logLine("installed ReadOnlyBinaryStream::read inline byte-trace probe");
    return true;
}

bool installPacketEndProbe(const MinecraftImage& image) {
    const auto functionAddress = image.base + target::kPacketEndCheckOffset;
    if (!addressIsExecutable(image, functionAddress) ||
        !matchesSignature(reinterpret_cast<const void*>(functionAddress),
                          target::kPacketEndCheckSignature)) {
        logLine("ERROR: packet completion signature mismatch; exact boundary probe disabled");
        return false;
    }
    if (mcpelauncher_patch == nullptr) {
        logLine("ERROR: launcher patch API unavailable; exact boundary probe disabled");
        return false;
    }

    std::array<std::uint8_t, 16> replacement{};
    constexpr std::uint32_t loadTarget = 0x58000050U;
    constexpr std::uint32_t branchTarget = 0xd61f0200U;
    const auto detour = reinterpret_cast<std::uintptr_t>(dobby_packet_end_detour);
    std::memcpy(replacement.data(), &loadTarget, sizeof(loadTarget));
    std::memcpy(replacement.data() + 4, &branchTarget, sizeof(branchTarget));
    std::memcpy(replacement.data() + 8, &detour, sizeof(detour));

    dobby_packet_end_continue = reinterpret_cast<void*>(functionAddress + replacement.size());
    auto* entry = reinterpret_cast<void*>(functionAddress);
    if (mcpelauncher_patch(entry, replacement.data(), replacement.size()) == nullptr ||
        std::memcmp(entry, replacement.data(), replacement.size()) != 0) {
        dobby_packet_end_continue = nullptr;
        logLine("ERROR: launcher rejected packet completion inline probe");
        return false;
    }
    logLine("installed exact packet completion boundary probe");
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

    const bool byteTraceProbe = installStreamProbe(image);
    const bool packetEndProbe = installPacketEndProbe(image);
    const bool streamProbe = byteTraceProbe || packetEndProbe;
    const bool warningHook = installViolationHook(image);
    if (!warningHook) {
        runtimeState().setHookStatus("failed: violation hook unavailable", false, streamProbe);
        recordLifecycleEvent("hook_error", "PacketViolationWarningPacket hook unavailable");
        return;
    }

    runtimeState().setHookStatus(
            packetEndProbe && byteTraceProbe
                    ? "active: violation + exact packet boundary + byte trace"
                    : streamProbe ? "active: violation + partial stream trace"
                                  : "active: violation only",
            true, streamProbe);
    recordLifecycleEvent(
            "hook_ready", packetEndProbe && byteTraceProbe
                    ? "packet violations, exact packet boundaries, and byte tracing active"
                    : streamProbe
                    ? "packet violations and partial stream tracing active"
                    : "packet violations active; stream tracing unavailable");
    logLine(std::string("READY: Dobby ") + kDobbyVersion + " developer diagnostics active");
}

} // namespace dobby
#endif
