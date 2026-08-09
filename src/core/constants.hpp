#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dobby {

inline constexpr char kDobbyVersion[] = "2.3.5";
inline constexpr char kMinecraftVersion[] = "1.26.40.5";
inline constexpr char kMinecraftBuildId[] = "5893edc8d56c93cbdb50e0f9436320236b78c89d";
inline constexpr char kAbi[] = "arm64-v8a";

inline constexpr std::size_t kDefaultHistoryLimit = 100;
inline constexpr std::size_t kMaximumHistoryLimit = 1000;
inline constexpr std::size_t kDefaultRawCaptureLimit = 2048;
inline constexpr std::size_t kMaximumRawCaptureLimit = 65536;

namespace target {

// All offsets and signatures are for the exact kMinecraftBuildId image.
inline constexpr std::uintptr_t kViolationGetIdOffset = 0x0cfa1cb4;
inline constexpr std::uintptr_t kViolationGetIdVtableSlotOffset = 0x120f7160;
inline constexpr std::array<std::uint8_t, 8> kViolationGetIdSignature{
        0x80, 0x13, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6};

inline constexpr std::uintptr_t kStreamReadOffset = 0x11a7b044;
inline constexpr std::uintptr_t kStreamReadVtableSlotOffset = 0x1249dac8;
inline constexpr std::array<std::uint8_t, 16> kStreamReadSignature{
        0xff, 0x83, 0x04, 0xd1, 0xfd, 0x7b, 0x0e, 0xa9,
        0xfc, 0x7b, 0x00, 0xf9, 0xf6, 0x57, 0x10, 0xa9};

inline constexpr std::uintptr_t kPacketEndCheckOffset = 0x11a7adb4;
inline constexpr std::array<std::uint8_t, 16> kPacketEndCheckSignature{
        0xff, 0x43, 0x02, 0xd1, 0xfd, 0x7b, 0x06, 0xa9,
        0xf5, 0x3b, 0x00, 0xf9, 0xf4, 0x4f, 0x08, 0xa9};

// PacketSchemaReader virtuals verified against the matching LeviLamina headers.
inline constexpr std::uintptr_t kSchemaPushMemberOffset = 0x0cbca71c;
inline constexpr std::uintptr_t kSchemaPushMemberVtableSlotOffset = 0x120dc6e0;
inline constexpr std::array<std::uint8_t, 8> kSchemaPushMemberSignature{
        0x20, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6};

inline constexpr std::uintptr_t kSchemaPushElementOffset = 0x0cbca730;
inline constexpr std::uintptr_t kSchemaPushElementVtableSlotOffset = 0x120dc6f0;
inline constexpr std::array<std::uint8_t, 4> kSchemaPushElementSignature{
        0xc0, 0x03, 0x5f, 0xd6};

inline constexpr std::uintptr_t kSchemaPopOffset = 0x0cbca734;
inline constexpr std::uintptr_t kSchemaPopVtableSlotOffset = 0x120dc6f8;
inline constexpr std::array<std::uint8_t, 4> kSchemaPopSignature{
        0xc0, 0x03, 0x5f, 0xd6};

// ActorRenderDispatcher::render(BaseActorRenderContext&, Actor&, bool).
inline constexpr std::uintptr_t kActorRenderOffset = 0x0a317de0;
inline constexpr std::array<std::uint8_t, 16> kActorRenderSignature{
        0xff, 0x43, 0x02, 0xd1, 0xeb, 0x2b, 0x03, 0x6d,
        0xe9, 0x23, 0x04, 0x6d, 0xfd, 0x7b, 0x05, 0xa9};

// Returns Actor::mBuiltInComponents.mAABBShapeComponent. AABB is the first
// member of that component, so its address is also the collision AABB address.
inline constexpr std::uintptr_t kActorGetAabbOffset = 0x0ec86fb4;
inline constexpr std::array<std::uint8_t, 8> kActorGetAabbSignature{
        0x00, 0x08, 0x41, 0xf9, 0xc0, 0x03, 0x5f, 0xd6};

// ClientLevel::getRuntimeActorList(). The method returns the complete active
// Actor* list and is used once per presented frame, matching Horion's entity
// enumeration model instead of relying on whichever actors a render pass saw.
inline constexpr std::uintptr_t kLevelGetRuntimeActorListOffset = 0x0f226d10;
inline constexpr std::array<std::uint8_t, 8> kLevelGetRuntimeActorListSignature{
        0x00, 0x38, 0x42, 0xf9, 0x87, 0x94, 0x01, 0x14};
inline constexpr std::size_t kLevelGetRuntimeActorListVtableSlot = 326;

// ClientLevel::forEachPlayer(std::function<bool(Player&)>). This supplements
// the general Actor* registry with the client-owned active-player registry.
inline constexpr std::uintptr_t kLevelForEachPlayerOffset = 0x0f225e4c;
inline constexpr std::array<std::uint8_t, 8> kLevelForEachPlayerSignature{
        0xff, 0x83, 0x01, 0xd1, 0xfd, 0x7b, 0x04, 0xa9};
inline constexpr std::size_t kLevelForEachPlayerVtableSlot = 223;

// ClientLevel::getPrimaryLocalPlayer(). The returned Player* is excluded from
// the overlay while every other active player remains visible.
inline constexpr std::uintptr_t kLevelGetPrimaryLocalPlayerOffset = 0x0f225818;
inline constexpr std::array<std::uint8_t, 8> kLevelGetPrimaryLocalPlayerSignature{
        0x00, 0x0c, 0x42, 0xf9, 0xce, 0x9a, 0xfa, 0x17};
inline constexpr std::size_t kLevelGetPrimaryLocalPlayerVtableSlot = 77;

// The ClientLevel primary vtable. The exact vptr check prevents calling the
// list getter through an unexpected ILevel implementation.
inline constexpr std::uintptr_t kClientLevelVtableOffset = 0x11ed28b0;

// BaseActorRenderContext::getProjectionMatrix(). This validates the context ->
// ScreenContext -> Camera path used by the passive overlay capture.
inline constexpr std::uintptr_t kProjectionMatrixGetterOffset = 0x0a5d8b94;
inline constexpr std::array<std::uint8_t, 16> kProjectionMatrixGetterSignature{
        0x08, 0x14, 0x40, 0xf9, 0x08, 0x0d, 0x40, 0xf9,
        0x00, 0x41, 0x02, 0x91, 0xc0, 0x03, 0x5f, 0xd6};

// BaseActorRenderContext::getViewMatrix(). It returns the Camera itself because
// the view MatrixStack is the first camera member.
inline constexpr std::uintptr_t kViewMatrixGetterOffset = 0x0a5d8ba4;
inline constexpr std::array<std::uint8_t, 12> kViewMatrixGetterSignature{
        0x08, 0x14, 0x40, 0xf9, 0x00, 0x0d, 0x40, 0xf9,
        0xc0, 0x03, 0x5f, 0xd6};

// BaseActorRenderContext::getCameraPosition(). Actor AABBs use world
// coordinates while Camera::mPosition is render-relative on this target.
inline constexpr std::uintptr_t kCameraPositionGetterOffset = 0x0a5d8b70;
inline constexpr std::array<std::uint8_t, 12> kCameraPositionGetterSignature{
        0x08, 0x54, 0x40, 0xf9, 0x00, 0xd1, 0x00, 0x91,
        0xc0, 0x03, 0x5f, 0xd6};

// Android libc++ gives MatrixStack a 0x48-byte layout. The validated
// projection getter above returns Camera + 0x90, proving two preceding
// MatrixStacks of that size. The camera vectors follow the third stack and
// the 0x40-byte inverse-view matrix.
inline constexpr std::ptrdiff_t kRenderContextScreenContextOffset = 0x28;
// Actor::getLevel() at image offset 0x0eca7920 loads this exact member.
// Nearby code confirms that 0x1c8 belongs to a different actor field.
inline constexpr std::ptrdiff_t kActorLevelOffset = 0x1d0;
inline constexpr std::uintptr_t kActorGetLevelOffset = 0x0eca7920;
inline constexpr std::array<std::uint8_t, 8> kActorGetLevelSignature{
        0x00, 0xe8, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6};
inline constexpr std::ptrdiff_t kScreenContextCameraOffset = 0x18;
inline constexpr std::ptrdiff_t kCameraProjectionStackOffset = 0x90;
inline constexpr std::ptrdiff_t kCameraRightOffset = 0x118;
inline constexpr std::ptrdiff_t kCameraUpOffset = 0x124;
inline constexpr std::ptrdiff_t kCameraForwardOffset = 0x130;
inline constexpr std::ptrdiff_t kCameraPositionOffset = 0x13c;

// Android libc++ std::deque<Matrix> fields inside MatrixStack.
inline constexpr std::ptrdiff_t kMatrixStackMapBeginOffset = 0x08;
inline constexpr std::ptrdiff_t kMatrixStackMapEndOffset = 0x10;
inline constexpr std::ptrdiff_t kMatrixStackStartOffset = 0x20;
inline constexpr std::ptrdiff_t kMatrixStackSizeOffset = 0x28;
inline constexpr std::size_t kMatrixBytes = 0x40;
inline constexpr std::size_t kMatricesPerDequeBlock = 64;

} // namespace target

} // namespace dobby
