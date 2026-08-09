#include "hooks/network_metrics_hook.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "hooks/minecraft_image.hpp"
#include "metrics/network_metrics.hpp"
#include "platform/launcher.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dobby {
namespace {

using RakNetPeerUpdateFn = void (*)(void* peer);
using LevelGetCurrentServerTickFn = std::uint64_t (*)(const void* level);

std::atomic_bool pingHookReady{false};
std::atomic_bool serverTickReady{false};
std::atomic_bool firstPingLogged{false};
std::atomic_bool firstTickLogged{false};
RakNetPeerUpdateFn originalRakNetPeerUpdate = nullptr;
LevelGetCurrentServerTickFn levelGetCurrentServerTick = nullptr;
MinecraftImage minecraftImage{};
std::uintptr_t expectedClientLevelVtable{};

template <class Value>
Value readObjectField(const void* object, std::ptrdiff_t offset) {
    Value value{};
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset,
                sizeof(value));
    return value;
}

void rakNetPeerUpdateDetour(void* peer) {
    if (originalRakNetPeerUpdate != nullptr)
        originalRakNetPeerUpdate(peer);
    if (peer == nullptr)
        return;
    const int lastPing = readObjectField<int>(
            peer, target::kRakNetPeerLastPingOffset);
    const int averagePing = readObjectField<int>(
            peer, target::kRakNetPeerAveragePingOffset);
    recordNativePing(peer, lastPing, averagePing);
    if (!firstPingLogged.exchange(true, std::memory_order_acq_rel))
        logLine("network metrics: native RakNet RTT observed");
}

bool validateServerTickSource(const MinecraftImage& image) {
    const auto getterAddress =
            image.base + target::kLevelGetCurrentServerTickOffset;
    const auto vtableAddress = image.base + target::kClientLevelVtableOffset;
    const auto slotAddress = vtableAddress +
            target::kLevelGetCurrentServerTickVtableSlot * sizeof(void*);
    if (!addressIsExecutable(image, getterAddress) ||
        !addressIsInImage(image, vtableAddress) ||
        !addressIsInImage(image, slotAddress) ||
        !matchesSignature(reinterpret_cast<const void*>(getterAddress),
                          target::kLevelGetCurrentServerTickSignature)) {
        return false;
    }
    std::uintptr_t slotTarget{};
    std::memcpy(&slotTarget, reinterpret_cast<const void*>(slotAddress),
                sizeof(slotTarget));
    if (slotTarget != getterAddress)
        return false;
    levelGetCurrentServerTick =
            reinterpret_cast<LevelGetCurrentServerTickFn>(getterAddress);
    expectedClientLevelVtable = vtableAddress;
    return true;
}

bool installPingHook(const MinecraftImage& image) {
    const auto updateAddress = image.base + target::kRakNetPeerUpdateOffset;
    const auto slotAddress =
            image.base + target::kRakNetPeerUpdateVtableSlotOffset;
    if (!addressIsExecutable(image, updateAddress) ||
        !addressIsInImage(image, slotAddress) ||
        !matchesSignature(reinterpret_cast<const void*>(updateAddress),
                          target::kRakNetPeerUpdateSignature)) {
        return false;
    }
    std::uintptr_t slotTarget{};
    std::memcpy(&slotTarget, reinterpret_cast<const void*>(slotAddress),
                sizeof(slotTarget));
    if (slotTarget != updateAddress || mcpelauncher_patch == nullptr)
        return false;

    originalRakNetPeerUpdate =
            reinterpret_cast<RakNetPeerUpdateFn>(updateAddress);
    auto detour = reinterpret_cast<std::uintptr_t>(rakNetPeerUpdateDetour);
    auto* slot = reinterpret_cast<void*>(slotAddress);
    if (mcpelauncher_patch(
                slot, &detour, sizeof(detour)) == nullptr) {
        originalRakNetPeerUpdate = nullptr;
        return false;
    }
    std::uintptr_t installedTarget{};
    std::memcpy(&installedTarget, slot, sizeof(installedTarget));
    if (installedTarget != detour) {
        originalRakNetPeerUpdate = nullptr;
        return false;
    }
    return true;
}

} // namespace

void installNetworkMetricsHook() {
    if (pingHookReady.load(std::memory_order_acquire) ||
        serverTickReady.load(std::memory_order_acquire)) {
        return;
    }
    const auto image = findMinecraftImage();
    if (image.base == 0) {
        logLine("ERROR: network metrics unavailable; Minecraft image missing");
        return;
    }
    minecraftImage = image;
    const bool tickReady = validateServerTickSource(image);
    serverTickReady.store(tickReady, std::memory_order_release);
    logLine(tickReady
            ? "network metrics: client server-tick clock validated"
            : "ERROR: TPS estimate unavailable; server-tick source mismatch");

    const bool pingReady = installPingHook(image);
    pingHookReady.store(pingReady, std::memory_order_release);
    logLine(pingReady
            ? "network metrics: RakNet ping hook installed"
            : "ERROR: ping overlay unavailable; RakNet layout mismatch");
}

bool networkPingHookInstalled() {
    return pingHookReady.load(std::memory_order_acquire);
}

bool serverTickSourceInstalled() {
    return serverTickReady.load(std::memory_order_acquire);
}

void captureClientServerTick(const void* level) {
    if (!serverTickReady.load(std::memory_order_acquire) || level == nullptr ||
        levelGetCurrentServerTick == nullptr || expectedClientLevelVtable == 0) {
        return;
    }
    const auto levelAddress = reinterpret_cast<std::uintptr_t>(level);
    if (levelAddress % alignof(void*) != 0)
        return;
    const auto vtable = readObjectField<std::uintptr_t>(level, 0);
    if (vtable != expectedClientLevelVtable ||
        !addressIsInImage(minecraftImage, vtable)) {
        return;
    }
    const std::uint64_t tick = levelGetCurrentServerTick(level);
    recordObservedServerTick(level, tick);
    if (!firstTickLogged.exchange(true, std::memory_order_acq_rel))
        logLine("network metrics: client server-tick clock observed");
}

} // namespace dobby

#else

namespace dobby {

void installNetworkMetricsHook() {}
bool networkPingHookInstalled() { return false; }
bool serverTickSourceInstalled() { return false; }
void captureClientServerTick(const void*) {}

} // namespace dobby

#endif
