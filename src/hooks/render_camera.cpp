#include "hooks/render_camera.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "platform/safe_memory.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace dobby {
namespace {

std::atomic_bool configured{false};
MinecraftImage configuredImage{};

template <class Value>
std::optional<Value> readProcessField(
        const void* object, std::ptrdiff_t offset) {
    if (object == nullptr)
        return std::nullopt;
    Value value{};
    const auto* source = static_cast<const std::byte*>(object) + offset;
    if (!copyReadableMemory(
                source,
                std::as_writable_bytes(std::span{&value, std::size_t{1}}))) {
        return std::nullopt;
    }
    return value;
}

bool readMatrixStackTop(
        const void* stackObject, std::array<float, 16>& output) {
    if (stackObject == nullptr)
        return false;
    const auto mapBegin = readProcessField<const std::byte*>(
            stackObject, target::kMatrixStackMapBeginOffset);
    const auto mapEnd = readProcessField<const std::byte*>(
            stackObject, target::kMatrixStackMapEndOffset);
    const auto start = readProcessField<std::size_t>(
            stackObject, target::kMatrixStackStartOffset);
    const auto size = readProcessField<std::size_t>(
            stackObject, target::kMatrixStackSizeOffset);
    if (!mapBegin || !mapEnd || !start || !size)
        return false;
    const auto beginAddress = reinterpret_cast<std::uintptr_t>(*mapBegin);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(*mapEnd);
    if (*mapBegin == nullptr || *mapEnd == nullptr ||
        endAddress <= beginAddress ||
        (endAddress - beginAddress) % sizeof(void*) != 0 ||
        endAddress - beginAddress > 128 * sizeof(void*) || *size == 0 ||
        *size > 128 ||
        *start > std::numeric_limits<std::size_t>::max() - *size) {
        return false;
    }

    const std::size_t element = *start + *size - 1;
    const std::size_t mapIndex = element / target::kMatricesPerDequeBlock;
    const std::size_t mapSize = (endAddress - beginAddress) / sizeof(void*);
    if (mapIndex >= mapSize)
        return false;
    const auto block = readProcessField<const std::byte*>(
            *mapBegin,
            static_cast<std::ptrdiff_t>(mapIndex * sizeof(void*)));
    if (!block || *block == nullptr)
        return false;
    const auto* matrix = reinterpret_cast<const float*>(
            *block + (element % target::kMatricesPerDequeBlock) *
                    target::kMatrixBytes);
    if (!copyReadableMemory(
                matrix,
                std::as_writable_bytes(std::span{output}))) {
        return false;
    }
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
    const auto levelLayoutProbe =
            image.base + target::kLevelRendererLevelLayoutProbeOffset;
    const auto levelUseProbe =
            image.base + target::kLevelRendererLevelUseProbeOffset;
    const auto levelCameraProbe =
            image.base + target::kLevelRenderCameraPointerProbeOffset;
    const auto rendererPositionProbe =
            image.base + target::kLevelRendererCameraPositionUseProbeOffset;
    if (!addressIsExecutable(image, projection) ||
        !addressIsExecutable(image, view) ||
        !addressIsExecutable(image, position) ||
        !addressIsExecutable(image, levelLayoutProbe) ||
        !addressIsExecutable(image, levelUseProbe) ||
        !addressIsExecutable(image, levelCameraProbe) ||
        !addressIsExecutable(image, rendererPositionProbe) ||
        !matchesSignature(reinterpret_cast<const void*>(projection),
                          target::kProjectionMatrixGetterSignature) ||
        !matchesSignature(reinterpret_cast<const void*>(view),
                          target::kViewMatrixGetterSignature) ||
        !matchesSignature(reinterpret_cast<const void*>(position),
                          target::kCameraPositionGetterSignature) ||
        !matchesSignature(
                reinterpret_cast<const void*>(levelLayoutProbe),
                target::kLevelRendererLevelLayoutProbeSignature) ||
        !matchesSignature(
                reinterpret_cast<const void*>(levelUseProbe),
                target::kLevelRendererLevelUseProbeSignature) ||
        !matchesSignature(
                reinterpret_cast<const void*>(levelCameraProbe),
                target::kLevelRenderCameraPointerProbeSignature) ||
        !matchesSignature(
                reinterpret_cast<const void*>(rendererPositionProbe),
                target::kLevelRendererCameraPositionUseProbeSignature)) {
        return false;
    }
    configuredImage = image;
    configured.store(true, std::memory_order_release);
    return true;
}

bool captureRenderCameraFrame(
        const void* renderContext, CameraFrame& output) {
    if (!configured.load(std::memory_order_acquire) ||
        renderContext == nullptr) {
        return false;
    }
    const auto screenContext = readProcessField<const void*>(
            renderContext, target::kRenderContextScreenContextOffset);
    const auto cameraState = readProcessField<const void*>(
            renderContext, target::kRenderContextCameraStateOffset);
    if (!screenContext || *screenContext == nullptr ||
        !cameraState || *cameraState == nullptr) {
        return false;
    }
    const auto camera = readProcessField<const void*>(
            *screenContext, target::kScreenContextCameraOffset);
    if (!camera || *camera == nullptr)
        return false;

    const auto position = readProcessField<Vec3f>(
            *cameraState, target::kRenderCameraStatePositionOffset);
    if (!position)
        return false;

    CameraFrame captured{};
    captured.position = *position;
    if (!readMatrixStackTop(*camera, captured.view) ||
        !readMatrixStackTop(
                static_cast<const std::byte*>(*camera) +
                        target::kCameraProjectionStackOffset,
                captured.projection) ||
        !validCameraFrame(captured)) {
        return false;
    }
    output = captured;
    return true;
}

bool captureLevelRenderCameraFrame(
        const void* levelRenderer, const void* renderContext,
        const void*& levelIdentity, CameraFrame& output,
        RenderCameraCaptureFailure& failure) {
    failure = RenderCameraCaptureFailure::none;
    levelIdentity = nullptr;
    if (!configured.load(std::memory_order_acquire)) {
        failure = RenderCameraCaptureFailure::notConfigured;
        return false;
    }
    if (levelRenderer == nullptr) {
        failure = RenderCameraCaptureFailure::levelRendererUnavailable;
        return false;
    }
    const auto rendererVtable = readProcessField<std::uintptr_t>(
            levelRenderer, 0);
    if (!rendererVtable ||
        *rendererVtable != configuredImage.base +
                target::kLevelRendererPlayerVtableOffset) {
        failure = RenderCameraCaptureFailure::levelRendererUnavailable;
        return false;
    }
    const auto level = readProcessField<const void*>(
            levelRenderer, target::kLevelRendererLevelOffset);
    if (!level || *level == nullptr) {
        failure = RenderCameraCaptureFailure::levelUnavailable;
        return false;
    }
    const auto levelVtable = readProcessField<std::uintptr_t>(*level, 0);
    if (!levelVtable ||
        *levelVtable != configuredImage.base + target::kClientLevelVtableOffset) {
        failure = RenderCameraCaptureFailure::levelUnavailable;
        return false;
    }
    if (renderContext == nullptr) {
        failure = RenderCameraCaptureFailure::renderContextUnavailable;
        return false;
    }
    const auto position = readProcessField<Vec3f>(
            levelRenderer, target::kLevelRendererCameraPositionOffset);
    if (!position) {
        failure = RenderCameraCaptureFailure::cameraPositionUnavailable;
        return false;
    }

    CameraFrame captured{};
    captured.position = *position;
    const auto camera = readProcessField<const void*>(
            renderContext, target::kLevelRenderCameraPointerOffset);
    if (!camera || *camera == nullptr) {
        failure = RenderCameraCaptureFailure::cameraUnavailable;
        return false;
    }
    if (!readMatrixStackTop(*camera, captured.view)) {
        failure = RenderCameraCaptureFailure::viewMatrixUnavailable;
        return false;
    }
    if (!readMatrixStackTop(
                static_cast<const std::byte*>(*camera) +
                        target::kCameraProjectionStackOffset,
                captured.projection)) {
        failure = RenderCameraCaptureFailure::projectionMatrixUnavailable;
        return false;
    }
    if (!validCameraFrame(captured)) {
        failure = RenderCameraCaptureFailure::invalidFrame;
        return false;
    }
    levelIdentity = *level;
    output = captured;
    return true;
}

} // namespace dobby

#endif
