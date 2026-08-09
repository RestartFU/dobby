#include "hooks/render_camera.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dobby {
namespace {

using ContextGetCameraPositionFn = const Vec3f* (*)(const void* renderContext);

std::atomic_bool configured{false};
ContextGetCameraPositionFn contextGetCameraPosition = nullptr;

template <class Value>
Value readObjectField(const void* object, std::ptrdiff_t offset) {
    Value value{};
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset,
                sizeof(value));
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
    const auto beginAddress = reinterpret_cast<std::uintptr_t>(mapBegin);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(mapEnd);
    if (mapBegin == nullptr || mapEnd == nullptr || endAddress <= beginAddress ||
        (endAddress - beginAddress) % sizeof(void*) != 0 ||
        endAddress - beginAddress > 128 * sizeof(void*) || size == 0 ||
        size > 128 || start > std::numeric_limits<std::size_t>::max() - size) {
        return false;
    }

    const std::size_t element = start + size - 1;
    const std::size_t mapIndex = element / target::kMatricesPerDequeBlock;
    const std::size_t mapSize = (endAddress - beginAddress) / sizeof(void*);
    if (mapIndex >= mapSize)
        return false;
    const auto* block = readObjectField<const std::byte*>(
            mapBegin, static_cast<std::ptrdiff_t>(mapIndex * sizeof(void*)));
    if (block == nullptr)
        return false;
    const auto* matrix = reinterpret_cast<const float*>(
            block + (element % target::kMatricesPerDequeBlock) *
                    target::kMatrixBytes);
    std::memcpy(output.data(), matrix, target::kMatrixBytes);
    for (const float value : output) {
        if (!std::isfinite(value) || std::fabs(value) >= 1.0e8F)
            return false;
    }
    return true;
}

} // namespace

bool configureRenderCameraCapture(const MinecraftImage& image) {
    if (configured.load(std::memory_order_acquire))
        return true;
    if (image.base == 0)
        return false;
    const auto projection = image.base + target::kProjectionMatrixGetterOffset;
    const auto view = image.base + target::kViewMatrixGetterOffset;
    const auto position = image.base + target::kCameraPositionGetterOffset;
    if (!addressIsExecutable(image, projection) ||
        !addressIsExecutable(image, view) ||
        !addressIsExecutable(image, position) ||
        !matchesSignature(reinterpret_cast<const void*>(projection),
                          target::kProjectionMatrixGetterSignature) ||
        !matchesSignature(reinterpret_cast<const void*>(view),
                          target::kViewMatrixGetterSignature) ||
        !matchesSignature(reinterpret_cast<const void*>(position),
                          target::kCameraPositionGetterSignature)) {
        return false;
    }
    contextGetCameraPosition =
            reinterpret_cast<ContextGetCameraPositionFn>(position);
    configured.store(true, std::memory_order_release);
    return true;
}

bool captureRenderCameraFrame(
        const void* renderContext, CameraFrame& output) {
    if (!configured.load(std::memory_order_acquire) || renderContext == nullptr ||
        contextGetCameraPosition == nullptr) {
        return false;
    }
    const void* screenContext = readObjectField<const void*>(
            renderContext, target::kRenderContextScreenContextOffset);
    if (screenContext == nullptr)
        return false;
    const void* camera = readObjectField<const void*>(
            screenContext, target::kScreenContextCameraOffset);
    const Vec3f* position = contextGetCameraPosition(renderContext);
    if (camera == nullptr || position == nullptr)
        return false;

    CameraFrame captured{};
    captured.position = *position;
    if (!readMatrixStackTop(camera, captured.view) ||
        !readMatrixStackTop(
                static_cast<const std::byte*>(camera) +
                        target::kCameraProjectionStackOffset,
                captured.projection) ||
        !validCameraFrame(captured)) {
        return false;
    }
    output = captured;
    return true;
}

} // namespace dobby

#endif
