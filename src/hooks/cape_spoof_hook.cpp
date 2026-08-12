#include "hooks/cape_spoof_hook.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "core/runtime_state.hpp"
#include "hooks/minecraft_image.hpp"
#include "hooks/outbound_packet_hook.hpp"
#include "hooks/persona_cape_repository_hook.hpp"
#include "platform/local_capes.hpp"
#include "platform/log.hpp"
#include "platform/safe_memory.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>

namespace dobby {
namespace {

using SetSkinFlagFn = void (*)(void* serializedSkinRef, bool value);
using GetSkinFlagFn = bool (*)(const void* serializedSkinRef);
using GetSkinMemberFn = const void* (*)(const void* serializedSkinRef);

std::atomic_bool capeHookReady{false};
std::atomic_bool invalidPacketLogged{false};
std::atomic_bool mutationFailureLogged{false};
std::uintptr_t expectedPlayerSkinVtable{};
SetSkinFlagFn setPremium = nullptr;
SetSkinFlagFn setPersona = nullptr;
SetSkinFlagFn setPersonaCapeOnClassic = nullptr;
GetSkinFlagFn getPremium = nullptr;
GetSkinFlagFn getPersona = nullptr;
GetSkinFlagFn getPersonaCapeOnClassic = nullptr;
GetSkinMemberFn getCapeId = nullptr;
GetSkinMemberFn getCapeImage = nullptr;

enum class CapePixelResult {
    notLocal,
    replaced,
    invalid,
};

template <class Value>
Value readObjectField(const void* object, std::ptrdiff_t offset) {
    Value value{};
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset,
                sizeof(value));
    return value;
}

bool objectHasVtable(const void* object, std::uintptr_t expectedVtable) {
    return object != nullptr && expectedVtable != 0 &&
            readObjectField<std::uintptr_t>(object, 0) == expectedVtable;
}

std::optional<std::string> readAndroidString(
        const void* object, std::size_t maximumLength) {
    constexpr std::size_t androidStringSize = 24;
    std::array<std::byte, androidStringSize> header{};
    if (!copyReadableMemory(object, header))
        return std::nullopt;
    const auto first = std::to_integer<std::uint8_t>(header[0]);
    const bool isLong = (first & 1U) != 0;
    std::size_t length{};
    const char* data{};
    if (isLong) {
        std::memcpy(&length, header.data() + sizeof(std::size_t), sizeof(length));
        std::memcpy(&data, header.data() + 2 * sizeof(std::size_t), sizeof(data));
    } else {
        length = static_cast<std::size_t>(first >> 1U);
        data = reinterpret_cast<const char*>(object) + 1;
    }
    if (length > maximumLength || (length != 0 && data == nullptr))
        return std::nullopt;
    std::string result(length, '\0');
    if (length != 0 &&
        !copyReadableMemory(
                data,
                std::span<std::byte>(
                        reinterpret_cast<std::byte*>(result.data()), length))) {
        return std::nullopt;
    }
    return result;
}

CapePixelResult replaceLocalCapePixels(
        void* serializedSkinRef, const void* implementation) {
    const void* capeIdObject = getCapeId(serializedSkinRef);
    const void* capeImage = getCapeImage(serializedSkinRef);
    const auto* expectedCapeId = static_cast<const std::byte*>(implementation) +
            target::kSerializedSkinImplCapeIdOffset;
    const auto* expectedCapeImage = static_cast<const std::byte*>(implementation) +
            target::kSerializedSkinImplCapeImageOffset;
    if (capeIdObject != expectedCapeId || capeImage != expectedCapeImage)
        return CapePixelResult::invalid;

    const auto capeId = readAndroidString(capeIdObject, 36);
    if (!capeId)
        return CapePixelResult::invalid;
    const LocalCape* localCape = findLocalCape(*capeId);
    if (localCape == nullptr)
        return CapePixelResult::notLocal;

    std::array<std::byte, 48> imageLayout{};
    if (!copyReadableMemory(capeImage, imageLayout))
        return CapePixelResult::invalid;
    std::uint32_t format{};
    std::uint32_t width{};
    std::uint32_t height{};
    void* pixels{};
    std::size_t pixelCount{};
    std::memcpy(
            &format, imageLayout.data() + target::kSkinImageFormatOffset,
            sizeof(format));
    std::memcpy(
            &width, imageLayout.data() + target::kSkinImageWidthOffset,
            sizeof(width));
    std::memcpy(
            &height, imageLayout.data() + target::kSkinImageHeightOffset,
            sizeof(height));
    std::memcpy(
            &pixels, imageLayout.data() + target::kSkinImageBlobDataOffset,
            sizeof(pixels));
    std::memcpy(
            &pixelCount, imageLayout.data() + target::kSkinImageBlobSizeOffset,
            sizeof(pixelCount));
    if (format != target::kRgba8ImageFormat ||
        width != kCapeTextureWidth || height != kCapeTextureHeight ||
        pixels == nullptr || pixelCount != kCapeTextureBytes) {
        return CapePixelResult::invalid;
    }

    std::array<std::byte, kCapeTextureBytes> readablePixels{};
    if (!copyReadableMemory(pixels, readablePixels))
        return CapePixelResult::invalid;
    std::memcpy(pixels, localCape->rgba.data(), localCape->rgba.size());
    return CapePixelResult::replaced;
}

void mutatePlayerSkinPacket(void* packet, std::int32_t packetId) {
    if (packetId != target::kPlayerSkinPacketId ||
        !capeHookReady.load(std::memory_order_acquire) ||
        !runtimeState().capeTestPackets()) {
        return;
    }
    if (!objectHasVtable(packet, expectedPlayerSkinVtable)) {
        if (!invalidPacketLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: cape test ignored PlayerSkinPacket vtable mismatch");
        return;
    }

    void* serializedSkinRef = static_cast<std::byte*>(packet) +
            target::kPlayerSkinSerializedSkinRefOffset;
    const void* implementation = readObjectField<const void*>(serializedSkinRef, 0);
    const void* owner = readObjectField<const void*>(serializedSkinRef,
                                                     sizeof(void*));
    if (implementation == nullptr || owner == nullptr) {
        if (!invalidPacketLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: cape test ignored empty SerializedSkinRef");
        return;
    }

    const auto pixelResult = replaceLocalCapePixels(
            serializedSkinRef, implementation);
    if (pixelResult == CapePixelResult::notLocal)
        return;
    if (pixelResult == CapePixelResult::invalid) {
        if (!mutationFailureLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: cape test ignored skin without an exact local cape image");
        return;
    }

    setPremium(serializedSkinRef, true);
    setPersona(serializedSkinRef, false);
    setPersonaCapeOnClassic(serializedSkinRef, true);
    if (!getPremium(serializedSkinRef) || getPersona(serializedSkinRef) ||
        !getPersonaCapeOnClassic(serializedSkinRef)) {
        if (!mutationFailureLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: cape test flag verification failed; disabling mutation");
        capeHookReady.store(false, std::memory_order_release);
        runtimeState().setCapeTestPacketsAvailable(false);
        return;
    }

    logLine("cape test: copied selected local pixels and marked PlayerSkinPacket premium/classic");
    recordLifecycleEvent(
            "cape_packet_mutated",
            "selected local cape pixels and ID preserved; premium/classic flags applied");
}

bool validateFunction(const MinecraftImage& image, std::uintptr_t offset,
                      const auto& signature) {
    const auto address = image.base + offset;
    return addressIsExecutable(image, address) &&
            matchesSignature(reinterpret_cast<const void*>(address), signature);
}

} // namespace

void installCapeSpoofHook() {
    if (capeHookReady.load(std::memory_order_acquire))
        return;
    runtimeState().setCapeTestPacketsAvailable(false);
    const auto image = findMinecraftImage();
    expectedPlayerSkinVtable =
            image.base + target::kPlayerSkinPacketVtableOffset;
    const auto getId = image.base + target::kPlayerSkinGetIdOffset;
    const auto getIdSlot = expectedPlayerSkinVtable +
            target::kPacketGetIdVtableSlot * sizeof(std::uintptr_t);
    std::uintptr_t currentGetId{};
    if (image.base == 0 || !outboundPacketHookInstalled() ||
        !personaCapeRepositoryHookInstalled() || localCapes().empty() ||
        !addressIsInImage(image, expectedPlayerSkinVtable) ||
        !addressIsInImage(image, getIdSlot) ||
        !validateFunction(image, target::kPlayerSkinGetIdOffset,
                          target::kPlayerSkinGetIdSignature) ||
        !validateFunction(image, target::kPlayerSkinLayoutProbeOffset,
                          target::kPlayerSkinLayoutProbeSignature) ||
        !validateFunction(image,
                          target::kSerializedSkinSetPersonaCapeOnClassicOffset,
                          target::kSerializedSkinSetPersonaCapeOnClassicSignature) ||
        !validateFunction(image, target::kSerializedSkinSetPremiumOffset,
                          target::kSerializedSkinSetPremiumSignature) ||
        !validateFunction(image, target::kSerializedSkinSetPersonaOffset,
                          target::kSerializedSkinSetPersonaSignature) ||
        !validateFunction(image, target::kSerializedSkinGetPremiumOffset,
                          target::kSerializedSkinGetPremiumSignature) ||
        !validateFunction(image, target::kSerializedSkinGetPersonaOffset,
                          target::kSerializedSkinGetPersonaSignature) ||
        !validateFunction(image,
                          target::kSerializedSkinGetPersonaCapeOnClassicOffset,
                          target::kSerializedSkinGetPersonaCapeOnClassicSignature) ||
        !validateFunction(image, target::kSerializedSkinGetCapeIdOffset,
                          target::kSerializedSkinGetCapeIdSignature) ||
        !validateFunction(image, target::kSerializedSkinGetCapeImageOffset,
                          target::kSerializedSkinGetCapeImageSignature)) {
        logLine("ERROR: cape entitlement test unavailable; target layout mismatch");
        return;
    }
    std::memcpy(&currentGetId, reinterpret_cast<const void*>(getIdSlot),
                sizeof(currentGetId));
    if (currentGetId != getId) {
        logLine("ERROR: cape entitlement test unavailable; PlayerSkinPacket vtable mismatch");
        return;
    }

    setPersonaCapeOnClassic = reinterpret_cast<SetSkinFlagFn>(
            image.base + target::kSerializedSkinSetPersonaCapeOnClassicOffset);
    setPremium = reinterpret_cast<SetSkinFlagFn>(
            image.base + target::kSerializedSkinSetPremiumOffset);
    setPersona = reinterpret_cast<SetSkinFlagFn>(
            image.base + target::kSerializedSkinSetPersonaOffset);
    getPremium = reinterpret_cast<GetSkinFlagFn>(
            image.base + target::kSerializedSkinGetPremiumOffset);
    getPersona = reinterpret_cast<GetSkinFlagFn>(
            image.base + target::kSerializedSkinGetPersonaOffset);
    getPersonaCapeOnClassic = reinterpret_cast<GetSkinFlagFn>(
            image.base + target::kSerializedSkinGetPersonaCapeOnClassicOffset);
    getCapeId = reinterpret_cast<GetSkinMemberFn>(
            image.base + target::kSerializedSkinGetCapeIdOffset);
    getCapeImage = reinterpret_cast<GetSkinMemberFn>(
            image.base + target::kSerializedSkinGetCapeImageOffset);
    if (!registerOutboundPacketHandler(mutatePlayerSkinPacket)) {
        logLine("ERROR: cape entitlement test unavailable; outbound router full");
        return;
    }

    capeHookReady.store(true, std::memory_order_release);
    runtimeState().setCapeTestPacketsAvailable(true);
    logLine(runtimeState().capeTestPackets()
            ? "cape entitlement test ready and enabled by saved preference"
            : "cape entitlement test ready; enable it in Mods > Dobby");
}

bool capeSpoofHookInstalled() {
    return capeHookReady.load(std::memory_order_acquire);
}

} // namespace dobby

#else

namespace dobby {

void installCapeSpoofHook() {}
bool capeSpoofHookInstalled() { return false; }

} // namespace dobby

#endif
