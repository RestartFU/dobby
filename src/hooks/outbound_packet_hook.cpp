#include "hooks/outbound_packet_hook.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "hooks/minecraft_image.hpp"
#include "platform/launcher.hpp"
#include "platform/log.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dobby {
namespace {

using SendFn = void (*)(void* sender, const void* packet);
using GetPacketIdFn = std::int32_t (*)(const void* packet);

constexpr std::size_t kMaximumOutboundHandlers = 8;
std::array<std::atomic<OutboundPacketHandler>, kMaximumOutboundHandlers>
        outboundHandlers{};
std::atomic_bool outboundHookReady{false};
SendFn originalSend = nullptr;
MinecraftImage minecraftImage{};

bool packetId(const void* packet, std::int32_t& result) {
    if (packet == nullptr)
        return false;
    std::uintptr_t vtable{};
    std::memcpy(&vtable, packet, sizeof(vtable));
    const auto slot = vtable +
            target::kPacketGetIdVtableSlot * sizeof(std::uintptr_t);
    if (!addressIsInImage(minecraftImage, vtable) ||
        !addressIsInImage(minecraftImage, slot)) {
        return false;
    }
    std::uintptr_t function{};
    std::memcpy(&function, reinterpret_cast<const void*>(slot), sizeof(function));
    if (!addressIsExecutable(minecraftImage, function))
        return false;
    result = reinterpret_cast<GetPacketIdFn>(function)(packet);
    return true;
}

void sendDetour(void* sender, const void* packet) {
    std::int32_t id{};
    if (packetId(packet, id)) {
        for (auto& slot : outboundHandlers) {
            const auto handler = slot.load(std::memory_order_acquire);
            if (handler != nullptr)
                handler(const_cast<void*>(packet), id);
        }
    }
    if (originalSend != nullptr)
        originalSend(sender, packet);
}

bool patchSlot(std::uintptr_t slotAddress, std::uintptr_t replacement) {
    auto value = replacement;
    auto* slot = reinterpret_cast<void*>(slotAddress);
    if (mcpelauncher_patch == nullptr ||
        mcpelauncher_patch(slot, &value, sizeof(value)) == nullptr) {
        return false;
    }
    std::uintptr_t installed{};
    std::memcpy(&installed, slot, sizeof(installed));
    return installed == replacement;
}

} // namespace

void installOutboundPacketHook() {
    if (outboundHookReady.load(std::memory_order_acquire))
        return;
    minecraftImage = findMinecraftImage();
    const auto function = minecraftImage.base + target::kLoopbackSendOffset;
    const auto slot = minecraftImage.base + target::kLoopbackSendVtableSlotOffset;
    std::uintptr_t current{};
    if (minecraftImage.base == 0 || mcpelauncher_patch == nullptr ||
        !addressIsExecutable(minecraftImage, function) ||
        !addressIsInImage(minecraftImage, slot) ||
        !matchesSignature(reinterpret_cast<const void*>(function),
                          target::kLoopbackSendSignature)) {
        logLine("ERROR: outbound packet hook unavailable; target mismatch");
        return;
    }
    std::memcpy(&current, reinterpret_cast<const void*>(slot), sizeof(current));
    if (current != function) {
        logLine("ERROR: outbound packet hook unavailable; vtable mismatch");
        return;
    }

    originalSend = reinterpret_cast<SendFn>(function);
    if (!patchSlot(slot, reinterpret_cast<std::uintptr_t>(sendDetour))) {
        originalSend = nullptr;
        logLine("ERROR: outbound packet hook unavailable; patch failed");
        return;
    }
    outboundHookReady.store(true, std::memory_order_release);
    logLine("outbound packet router: LoopbackPacketSender::send hooked");
}

bool outboundPacketHookInstalled() {
    return outboundHookReady.load(std::memory_order_acquire);
}

bool registerOutboundPacketHandler(OutboundPacketHandler handler) {
    if (handler == nullptr)
        return false;
    for (auto& slot : outboundHandlers) {
        if (slot.load(std::memory_order_acquire) == handler)
            return true;
    }
    for (auto& slot : outboundHandlers) {
        OutboundPacketHandler expected = nullptr;
        if (slot.compare_exchange_strong(
                    expected, handler, std::memory_order_release,
                    std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

} // namespace dobby

#else

namespace dobby {

void installOutboundPacketHook() {}
bool outboundPacketHookInstalled() { return false; }
bool registerOutboundPacketHandler(OutboundPacketHandler) { return false; }

} // namespace dobby

#endif
