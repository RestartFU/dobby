#include "hooks/persona_cape_repository_hook.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "core/runtime_state.hpp"
#include "hooks/minecraft_image.hpp"
#include "platform/launcher.hpp"
#include "platform/local_capes.hpp"
#include "platform/log.hpp"
#include "platform/safe_memory.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" void* dobby_persona_manager_lookup_continue = nullptr;
extern "C" void* dobby_persona_manager_insert_continue = nullptr;

extern "C" const void* dobby_persona_manager_lookup_original(
        void* manager, const void* pieceId);

namespace dobby {
extern "C" const void* dobby_lookup_local_persona_piece(
        void* manager, const void* pieceId);
extern "C" void dobby_prepare_persona_manager_insert(
        void* manager, const void* pieceId);
} // namespace dobby

extern "C" [[gnu::naked]] const void* dobby_persona_manager_lookup_original(
        void*, const void*) {
    asm volatile(
            "stp x29, x30, [sp, #-32]!\n"
            "str x19, [sp, #16]\n"
            "mov x29, sp\n"
            "ldrb w8, [x1]\n"
            "adrp x16, dobby_persona_manager_lookup_continue\n"
            "ldr x16, [x16, :lo12:dobby_persona_manager_lookup_continue]\n"
            "br x16\n");
}

extern "C" [[gnu::naked]] void dobby_persona_manager_lookup_detour() {
    asm volatile(
            "sub sp, sp, #96\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "str x30, [sp, #80]\n"
            "bl dobby_lookup_local_persona_piece\n"
            "cbz x0, 1f\n"
            "ldr x30, [sp, #80]\n"
            "add sp, sp, #96\n"
            "ret\n"
            "1:\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldr x30, [sp, #80]\n"
            "add sp, sp, #96\n"
            "stp x29, x30, [sp, #-32]!\n"
            "str x19, [sp, #16]\n"
            "mov x29, sp\n"
            "ldrb w8, [x1]\n"
            "adrp x16, dobby_persona_manager_lookup_continue\n"
            "ldr x16, [x16, :lo12:dobby_persona_manager_lookup_continue]\n"
            "br x16\n");
}

extern "C" [[gnu::naked]] void dobby_persona_manager_insert_detour() {
    asm volatile(
            "sub sp, sp, #96\n"
            "stp x0, x1, [sp, #0]\n"
            "stp x2, x3, [sp, #16]\n"
            "stp x4, x5, [sp, #32]\n"
            "stp x6, x7, [sp, #48]\n"
            "str x30, [sp, #80]\n"
            "bl dobby_prepare_persona_manager_insert\n"
            "ldp x0, x1, [sp, #0]\n"
            "ldp x2, x3, [sp, #16]\n"
            "ldp x4, x5, [sp, #32]\n"
            "ldp x6, x7, [sp, #48]\n"
            "ldr x30, [sp, #80]\n"
            "add sp, sp, #96\n"
            "sub sp, sp, #112\n"
            "stp x29, x30, [sp, #32]\n"
            "str x25, [sp, #48]\n"
            "stp x24, x23, [sp, #64]\n"
            "adrp x16, dobby_persona_manager_insert_continue\n"
            "ldr x16, [x16, :lo12:dobby_persona_manager_insert_continue]\n"
            "br x16\n");
}

namespace dobby {
namespace {

constexpr std::string_view kPanCapeId{
        "ef479b6d-7072-47aa-8985-0f025cd24cdb"};
constexpr std::size_t kAndroidStringSize = 24;
constexpr std::size_t kMaximumRequestedPieceTypes = 32;
constexpr std::size_t kMaximumNativeOwnedPieces = 512;
constexpr std::size_t kMaximumCapeCachePieces =
        kMaximumNativeOwnedPieces + kMaximumLocalCapes;

struct NativeUuid {
    std::uint64_t high{};
    std::uint64_t low{};

    bool operator==(const NativeUuid&) const = default;
};

struct NativeVector {
    void* begin{};
    void* end{};
    void* capacity{};
};

struct FakePersonaPiece {
    alignas(8) std::array<std::byte, target::kPersonaPieceSize> bytes{};
    bool ready{};
};

using PieceLookupFn = const void* (*)(void* repository, const void* pieceId);
using OwnedPiecesFn = NativeVector (*)(
        void* repository, const NativeVector* pieceTypes);

static_assert(sizeof(std::string) == kAndroidStringSize);
static_assert(sizeof(NativeUuid) == 16);
static_assert(sizeof(NativeVector) == 24);

std::atomic_bool repositoryHookReady{false};
std::atomic_bool injectedListLogged{false};
std::atomic_bool injectedCacheLogged{false};
std::atomic_bool resolvedLocalPieceLogged{false};
std::atomic_bool invalidPieceLogged{false};
std::atomic<void*> capturedPersonaManager{nullptr};
PieceLookupFn originalPieceLookup = nullptr;
OwnedPiecesFn originalOwnedPieces = nullptr;
std::array<FakePersonaPiece, kMaximumLocalCapes> fakePieces{};
std::mutex fakePieceMutex;

template <class Value>
std::optional<Value> readMemory(const void* address) {
    Value value{};
    if (!copyReadableMemory(
                address,
                std::span<std::byte>(
                        reinterpret_cast<std::byte*>(&value), sizeof(value)))) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> readAndroidString(
        const void* object, std::size_t maximumLength = 80) {
    std::array<std::byte, kAndroidStringSize> header{};
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

int hexadecimalDigit(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

std::optional<NativeUuid> parseUuid(std::string_view value) {
    if (!validLocalCapeId(value))
        return std::nullopt;
    NativeUuid result;
    std::size_t nibbleIndex = 0;
    for (const char character : value) {
        if (character == '-')
            continue;
        const int digit = hexadecimalDigit(character);
        if (digit < 0)
            return std::nullopt;
        auto& half = nibbleIndex < 16 ? result.high : result.low;
        half = (half << 4U) | static_cast<std::uint64_t>(digit);
        ++nibbleIndex;
    }
    return nibbleIndex == 32 ? std::optional<NativeUuid>(result) : std::nullopt;
}

std::optional<std::size_t> vectorElementCount(
        const NativeVector& vector, std::size_t elementSize,
        std::size_t maximumElements) {
    const auto begin = reinterpret_cast<std::uintptr_t>(vector.begin);
    const auto end = reinterpret_cast<std::uintptr_t>(vector.end);
    const auto capacity = reinterpret_cast<std::uintptr_t>(vector.capacity);
    if (begin == 0 || end == 0 || capacity == 0) {
        return begin == 0 && end == 0 && capacity == 0
                ? std::optional<std::size_t>(0)
                : std::nullopt;
    }
    if (end < begin || capacity < end || elementSize == 0)
        return std::nullopt;
    const auto bytes = end - begin;
    const auto capacityBytes = capacity - begin;
    if (bytes % elementSize != 0 || capacityBytes % elementSize != 0)
        return std::nullopt;
    const auto count = bytes / elementSize;
    const auto capacityCount = capacityBytes / elementSize;
    if (count > maximumElements || capacityCount > maximumElements * 4)
        return std::nullopt;
    return count;
}

bool requestedCapes(const NativeVector* pieceTypes) {
    const auto layout = readMemory<NativeVector>(pieceTypes);
    if (!layout)
        return false;
    const auto count = vectorElementCount(
            *layout, sizeof(std::int32_t), kMaximumRequestedPieceTypes);
    if (!count || *count == 0)
        return false;
    std::array<std::int32_t, kMaximumRequestedPieceTypes> values{};
    if (!copyReadableMemory(
                layout->begin,
                std::span<std::byte>(
                        reinterpret_cast<std::byte*>(values.data()),
                        *count * sizeof(values[0])))) {
        return false;
    }
    for (std::size_t index = 0; index < *count; ++index) {
        if (values[index] == target::kPersonaCapePieceType)
            return true;
    }
    return false;
}

bool appendLocalCapeUuids(NativeVector& result) {
    const auto oldCount = vectorElementCount(
            result, sizeof(NativeUuid), kMaximumNativeOwnedPieces);
    if (!oldCount)
        return false;

    std::vector<NativeUuid> values(*oldCount);
    if (*oldCount != 0 &&
        !copyReadableMemory(
                result.begin,
                std::span<std::byte>(
                        reinterpret_cast<std::byte*>(values.data()),
                        values.size() * sizeof(values[0])))) {
        return false;
    }
    for (const auto& cape : localCapes()) {
        const auto uuid = parseUuid(cape.descriptor.pieceId);
        if (!uuid)
            return false;
        bool duplicate = false;
        for (const auto& existing : values) {
            if (existing == *uuid) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            values.push_back(*uuid);
    }
    if (values.size() == *oldCount)
        return true;
    if (values.size() > kMaximumNativeOwnedPieces + kMaximumLocalCapes)
        return false;

    const std::size_t bytes = values.size() * sizeof(NativeUuid);
    void* replacement = std::malloc(bytes);
    if (replacement == nullptr)
        return false;
    std::memcpy(replacement, values.data(), bytes);
    std::free(result.begin);
    result.begin = replacement;
    result.end = static_cast<std::byte*>(replacement) + bytes;
    result.capacity = result.end;
    return true;
}

std::vector<std::string>* capeCacheVector(void* manager) {
    if (manager == nullptr)
        return nullptr;
    const auto vectorArray = readMemory<std::uintptr_t>(
            static_cast<const std::byte*>(manager) +
            target::kPersonaManagerPiecesByTypeOffset);
    if (!vectorArray || *vectorArray == 0)
        return nullptr;
    auto* vector = reinterpret_cast<std::vector<std::string>*>(
            *vectorArray + static_cast<std::uintptr_t>(
                    target::kPersonaCapePieceType) * sizeof(NativeVector));
    const auto layout = readMemory<NativeVector>(vector);
    if (!layout || !vectorElementCount(
                           *layout, kAndroidStringSize,
                           kMaximumCapeCachePieces)) {
        return nullptr;
    }
    return vector;
}

bool synchronizeCapeCache(void* manager, bool enabled) {
    auto* vector = capeCacheVector(manager);
    auto* mutex = manager == nullptr
            ? nullptr
            : reinterpret_cast<std::mutex*>(
                      static_cast<std::byte*>(manager) +
                      target::kPersonaManagerMutexOffset);
    if (vector == nullptr || mutex == nullptr)
        return false;

    std::lock_guard lock(*mutex);
    const auto layout = readMemory<NativeVector>(vector);
    const auto count = layout
            ? vectorElementCount(
                      *layout, kAndroidStringSize, kMaximumCapeCachePieces)
            : std::nullopt;
    if (!count)
        return false;

    auto isLocalId = [](const std::string& value) {
        return findLocalCape(value) != nullptr;
    };
    vector->erase(
            std::remove_if(vector->begin(), vector->end(), isLocalId),
            vector->end());
    if (enabled) {
        vector->reserve(vector->size() + localCapes().size());
        for (const auto& cape : localCapes())
            vector->push_back(cape.descriptor.pieceId);
    }
    return true;
}

bool validPanPiece(
        const void* piece,
        const std::array<std::byte, target::kPersonaPieceSize>& snapshot) {
    if (!piece)
        return false;
    const auto pieceId = readAndroidString(
            static_cast<const std::byte*>(piece) +
            target::kPersonaPieceIdOffset);
    std::int32_t pieceType{};
    NativeUuid firstUuid{};
    NativeUuid packUuid{};
    std::memcpy(
            &pieceType,
            snapshot.data() + target::kPersonaPieceTypeOffset,
            sizeof(pieceType));
    std::memcpy(
            &firstUuid,
            snapshot.data() + target::kPersonaPieceUuidOffset,
            sizeof(firstUuid));
    std::memcpy(
            &packUuid,
            snapshot.data() + target::kPersonaPiecePackUuidOffset,
            sizeof(packUuid));
    const auto expectedUuid = parseUuid(kPanCapeId);
    return pieceId && *pieceId == kPanCapeId && expectedUuid &&
            pieceType == target::kPersonaCapePieceType &&
            firstUuid == *expectedUuid && packUuid == *expectedUuid;
}

const void* buildFakePieceFromPan(const void* panPiece, std::size_t index) {
    const auto& capes = localCapes();
    if (index >= capes.size() || index >= fakePieces.size()) {
        return nullptr;
    }

    std::lock_guard lock(fakePieceMutex);
    auto& fake = fakePieces[index];
    if (fake.ready)
        return fake.bytes.data();

    if (!copyReadableMemory(panPiece, fake.bytes) ||
        !validPanPiece(panPiece, fake.bytes)) {
        if (!invalidPieceLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: local cape repository rejected the Pan Cape template");
        return nullptr;
    }

    const auto uuid = parseUuid(capes[index].descriptor.pieceId);
    if (!uuid)
        return nullptr;
    std::memcpy(
            fake.bytes.data() + target::kPersonaPieceIdOffset,
            &capes[index].descriptor.pieceId, sizeof(std::string));
    std::memcpy(
            fake.bytes.data() + target::kPersonaPieceUuidOffset,
            &*uuid, sizeof(*uuid));
    // Keep the validated Pan source-pack UUID and paths. Only the piece ID is
    // synthetic; changing the pack identity would make native resource lookup
    // point at an unloaded pack and invalidate the tile.
    fake.ready = true;
    return fake.bytes.data();
}

const void* buildFakePiece(void* repository, std::size_t index) {
    if (originalPieceLookup == nullptr)
        return nullptr;
    static const std::string panCapeId(kPanCapeId);
    return buildFakePieceFromPan(
            originalPieceLookup(repository, &panCapeId), index);
}

const void* localPieceLookup(void* repository, const void* pieceId) {
    if (originalPieceLookup == nullptr)
        return nullptr;
    if (!repositoryHookReady.load(std::memory_order_acquire) ||
        !runtimeState().capeTestPackets()) {
        return originalPieceLookup(repository, pieceId);
    }
    const auto id = readAndroidString(pieceId, 36);
    if (!id)
        return originalPieceLookup(repository, pieceId);

    const auto& capes = localCapes();
    for (std::size_t index = 0; index < capes.size(); ++index) {
        if (capes[index].descriptor.pieceId == *id) {
            const void* piece = buildFakePiece(repository, index);
            return piece == nullptr
                    ? originalPieceLookup(repository, pieceId)
                    : piece;
        }
    }
    return originalPieceLookup(repository, pieceId);
}

NativeVector localOwnedPieces(
        void* repository, const NativeVector* pieceTypes) {
    NativeVector result = originalOwnedPieces(repository, pieceTypes);
    if (!repositoryHookReady.load(std::memory_order_acquire) ||
        !runtimeState().capeTestPackets() || !requestedCapes(pieceTypes)) {
        return result;
    }
    if (!appendLocalCapeUuids(result)) {
        if (!invalidPieceLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: local cape repository rejected native owned-piece layout");
        return result;
    }
    if (!injectedListLogged.exchange(true, std::memory_order_acq_rel)) {
        logLine("local cape repository: injected native owned-cape UUIDs");
        recordLifecycleEvent(
                "local_cape_repository_injected",
                "native owned-cape list extended for explicit test mode");
    }
    return result;
}

bool validateFunction(const MinecraftImage& image, std::uintptr_t offset,
                      const auto& signature) {
    const auto address = image.base + offset;
    return addressIsExecutable(image, address) &&
            matchesSignature(reinterpret_cast<const void*>(address), signature);
}

bool patchPointer(std::uintptr_t slot, std::uintptr_t replacement) {
    auto* address = reinterpret_cast<void*>(slot);
    if (mcpelauncher_patch(
                address, &replacement, sizeof(replacement)) == nullptr) {
        return false;
    }
    const auto installed = readMemory<std::uintptr_t>(address);
    return installed && *installed == replacement;
}

bool patchEntry(std::uintptr_t entry, std::uintptr_t detour) {
    std::array<std::uint8_t, 16> replacement{};
    constexpr std::uint32_t loadTarget = 0x58000050U;
    constexpr std::uint32_t branchTarget = 0xd61f0200U;
    std::memcpy(replacement.data(), &loadTarget, sizeof(loadTarget));
    std::memcpy(
            replacement.data() + sizeof(loadTarget), &branchTarget,
            sizeof(branchTarget));
    std::memcpy(replacement.data() + 8, &detour, sizeof(detour));
    auto* address = reinterpret_cast<void*>(entry);
    return mcpelauncher_patch(
                   address, replacement.data(), replacement.size()) != nullptr &&
            std::memcmp(address, replacement.data(), replacement.size()) == 0;
}

template <std::size_t Size>
void restoreEntry(
        std::uintptr_t entry, const std::array<std::uint8_t, Size>& signature) {
    static_assert(Size == 16);
    static_cast<void>(mcpelauncher_patch(
            reinterpret_cast<void*>(entry),
            const_cast<std::uint8_t*>(signature.data()), signature.size()));
}

} // namespace

extern "C" const void* dobby_lookup_local_persona_piece(
        void* manager, const void* pieceId) {
    if (!repositoryHookReady.load(std::memory_order_acquire) ||
        !runtimeState().capeTestPackets()) {
        return nullptr;
    }
    const auto id = readAndroidString(pieceId, 36);
    if (!id)
        return nullptr;

    const auto& capes = localCapes();
    for (std::size_t index = 0; index < capes.size(); ++index) {
        if (capes[index].descriptor.pieceId != *id)
            continue;
        static const std::string panCapeId(kPanCapeId);
        const void* piece = buildFakePieceFromPan(
                dobby_persona_manager_lookup_original(manager, &panCapeId),
                index);
        if (piece != nullptr &&
            !resolvedLocalPieceLogged.exchange(true, std::memory_order_acq_rel)) {
            logLine("local cape repository: native manager resolved a local cape piece");
        }
        return piece;
    }
    return nullptr;
}

extern "C" void dobby_prepare_persona_manager_insert(
        void* manager, const void* pieceId) {
    const auto id = readAndroidString(pieceId, 36);
    if (!id || *id != kPanCapeId)
        return;
    capturedPersonaManager.store(manager, std::memory_order_release);
    if (!repositoryHookReady.load(std::memory_order_acquire) ||
        !runtimeState().capeTestPackets()) {
        return;
    }
    if (!synchronizeCapeCache(manager, true)) {
        if (!invalidPieceLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: local cape repository rejected the native cape cache layout");
        return;
    }
    if (!injectedCacheLogged.exchange(true, std::memory_order_acq_rel)) {
        logLine("local cape repository: injected local IDs into native persona_capes cache");
        recordLifecycleEvent(
                "local_cape_repository_injected",
                "native persona_capes manager cache extended for explicit test mode");
    }
}

void installPersonaCapeRepositoryHook() {
    if (repositoryHookReady.load(std::memory_order_acquire))
        return;
    if (localCapes().empty()) {
        logLine("ERROR: local cape repository unavailable; no validated cape data");
        return;
    }
    const auto image = findMinecraftImage();
    if (image.base == 0 || mcpelauncher_patch == nullptr) {
        logLine("ERROR: local cape repository unavailable; launcher bridge missing");
        return;
    }

    const auto vtable = image.base + target::kPersonaRepositoryVtableOffset;
    const auto lookupSlot = vtable +
            target::kPersonaRepositoryLookupSlot * sizeof(std::uintptr_t);
    const auto ownedPiecesSlot = vtable +
            target::kPersonaRepositoryOwnedPiecesSlot * sizeof(std::uintptr_t);
    const auto expectedLookup =
            image.base + target::kPersonaRepositoryLookupOffset;
    const auto expectedOwnedPieces =
            image.base + target::kPersonaRepositoryOwnedPiecesOffset;
    const auto managerLookup =
            image.base + target::kPersonaManagerLookupOffset;
    const auto managerInsert =
            image.base + target::kPersonaManagerInsertOffset;
    const auto currentLookup = readMemory<std::uintptr_t>(
            reinterpret_cast<const void*>(lookupSlot));
    const auto currentOwnedPieces = readMemory<std::uintptr_t>(
            reinterpret_cast<const void*>(ownedPiecesSlot));
    if (!addressIsInImage(image, vtable) ||
        !addressIsInImage(image, lookupSlot) ||
        !addressIsInImage(image, ownedPiecesSlot) ||
        !validateFunction(
                image, target::kPersonaRepositoryLookupOffset,
                target::kPersonaRepositoryLookupSignature) ||
        !validateFunction(
                image, target::kPersonaRepositoryOwnedPiecesOffset,
                target::kPersonaRepositoryOwnedPiecesSignature) ||
        !validateFunction(
                image, target::kPersonaManagerLookupOffset,
                target::kPersonaManagerLookupSignature) ||
        !validateFunction(
                image, target::kPersonaManagerInsertOffset,
                target::kPersonaManagerInsertSignature) ||
        !currentLookup || *currentLookup != expectedLookup ||
        !currentOwnedPieces || *currentOwnedPieces != expectedOwnedPieces) {
        logLine("ERROR: local cape repository unavailable; target layout mismatch");
        return;
    }

    originalPieceLookup = reinterpret_cast<PieceLookupFn>(*currentLookup);
    originalOwnedPieces = reinterpret_cast<OwnedPiecesFn>(*currentOwnedPieces);
    const auto lookupReplacement =
            reinterpret_cast<std::uintptr_t>(localPieceLookup);
    const auto ownedPiecesReplacement =
            reinterpret_cast<std::uintptr_t>(localOwnedPieces);
    dobby_persona_manager_lookup_continue =
            reinterpret_cast<void*>(managerLookup + 16);
    dobby_persona_manager_insert_continue =
            reinterpret_cast<void*>(managerInsert + 16);
    const bool managerLookupPatched = patchEntry(
            managerLookup,
            reinterpret_cast<std::uintptr_t>(
                    dobby_persona_manager_lookup_detour));
    const bool managerInsertPatched = managerLookupPatched && patchEntry(
            managerInsert,
            reinterpret_cast<std::uintptr_t>(
                    dobby_persona_manager_insert_detour));
    if (!managerLookupPatched || !managerInsertPatched ||
        !patchPointer(lookupSlot, lookupReplacement) ||
        !patchPointer(ownedPiecesSlot, ownedPiecesReplacement)) {
        static_cast<void>(patchPointer(lookupSlot, *currentLookup));
        static_cast<void>(patchPointer(ownedPiecesSlot, *currentOwnedPieces));
        if (managerLookupPatched) {
            restoreEntry(
                    managerLookup, target::kPersonaManagerLookupSignature);
        }
        if (managerInsertPatched) {
            restoreEntry(
                    managerInsert, target::kPersonaManagerInsertSignature);
        }
        dobby_persona_manager_lookup_continue = nullptr;
        dobby_persona_manager_insert_continue = nullptr;
        originalPieceLookup = nullptr;
        originalOwnedPieces = nullptr;
        logLine("ERROR: local cape repository unavailable; native patch rejected");
        return;
    }

    repositoryHookReady.store(true, std::memory_order_release);
    logLine("local cape repository ready: native manager cache, lookup, and owned-list hooks installed");
}

bool personaCapeRepositoryHookInstalled() {
    return repositoryHookReady.load(std::memory_order_acquire);
}

bool setPersonaCapeRepositoryEnabled(bool enabled) {
    if (!repositoryHookReady.load(std::memory_order_acquire))
        return false;
    void* manager = capturedPersonaManager.load(std::memory_order_acquire);
    if (manager == nullptr)
        return false;
    const bool synchronized = synchronizeCapeCache(manager, enabled);
    if (synchronized) {
        logLine(enabled
                ? "local cape repository: native persona_capes cache enabled"
                : "local cape repository: native persona_capes cache restored");
    }
    return synchronized;
}

} // namespace dobby

#else

namespace dobby {

void installPersonaCapeRepositoryHook() {}
bool personaCapeRepositoryHookInstalled() { return false; }
bool setPersonaCapeRepositoryEnabled(bool) { return false; }

} // namespace dobby

#endif
