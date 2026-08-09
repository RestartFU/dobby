#include "diagnostics/stream_probe.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace dobby {
namespace {

constexpr std::size_t kMaximumTraceAttempts = 96;

template <class T>
T readUnaligned(const std::byte* address) {
    T value{};
    std::memcpy(&value, address, sizeof(value));
    return value;
}

struct ActiveTrace {
    const void* stream{};
    const std::uint8_t* data{};
    std::size_t size{};
    std::size_t previousOffset{};
    std::array<StreamReadAttempt, kMaximumTraceAttempts> attempts{};
    std::size_t attemptCount{};
    std::size_t nextAttempt{};
};

struct TimedFailure {
    StreamFailure failure;
    std::chrono::steady_clock::time_point capturedAt;
};

thread_local ActiveTrace activeTrace;
std::mutex failureMutex;
std::optional<TimedFailure> lastFailure;

void resetTrace(const void* stream, const StreamView& view) {
    activeTrace.stream = stream;
    activeTrace.data = view.data;
    activeTrace.size = view.size;
    activeTrace.previousOffset = view.readPointer;
    activeTrace.attemptCount = 0;
    activeTrace.nextAttempt = 0;
}

} // namespace

std::optional<StreamView> inspectStream(const void* stream) {
    if (stream == nullptr)
        return std::nullopt;
    const auto* bytes = static_cast<const std::byte*>(stream);
    StreamView result;
    result.data = readUnaligned<const std::uint8_t*>(bytes + kStreamViewDataOffset);
    result.size = readUnaligned<std::size_t>(bytes + kStreamViewSizeOffset);
    result.readPointer = readUnaligned<std::size_t>(bytes + kStreamReadPointerOffset);
    result.overflowed = readUnaligned<std::uint8_t>(bytes + kStreamOverflowOffset) != 0;
    if (result.data == nullptr || result.size > (1024ULL * 1024ULL * 1024ULL) ||
        result.readPointer > result.size) {
        return std::nullopt;
    }
    return result;
}

void captureStreamReadAttempt(const void* stream, std::size_t requested, std::size_t rawCaptureLimit) {
    const auto view = inspectStream(stream);
    if (!view)
        return;

    if (activeTrace.stream != stream || activeTrace.data != view->data ||
        activeTrace.size != view->size || view->readPointer < activeTrace.previousOffset) {
        resetTrace(stream, *view);
    }

    const std::size_t available = view->size - view->readPointer;
    const bool overflow = view->overflowed || requested > available;
    activeTrace.attempts[activeTrace.nextAttempt] = StreamReadAttempt{
            view->readPointer, requested, available, overflow};
    activeTrace.nextAttempt = (activeTrace.nextAttempt + 1) % kMaximumTraceAttempts;
    activeTrace.attemptCount = std::min(activeTrace.attemptCount + 1, kMaximumTraceAttempts);
    activeTrace.previousOffset = view->readPointer;

    if (!overflow)
        return;

    StreamFailure failure;
    failure.viewSize = view->size;
    failure.failureOffset = view->readPointer;
    failure.requested = requested;
    failure.available = available;
    failure.overflowBeforeRead = view->overflowed;
    const std::size_t captureSize = std::min(view->size, rawCaptureLimit);
    failure.rawBytes.assign(view->data, view->data + captureSize);
    failure.rawBytesTruncated = captureSize < view->size;
    failure.attempts.reserve(activeTrace.attemptCount);
    const std::size_t first =
            (activeTrace.nextAttempt + kMaximumTraceAttempts - activeTrace.attemptCount) %
            kMaximumTraceAttempts;
    for (std::size_t index = 0; index < activeTrace.attemptCount; ++index) {
        failure.attempts.push_back(
                activeTrace.attempts[(first + index) % kMaximumTraceAttempts]);
    }

    std::lock_guard lock(failureMutex);
    lastFailure = TimedFailure{std::move(failure), std::chrono::steady_clock::now()};
}

std::optional<StreamFailure> recentStreamFailure(std::chrono::milliseconds maximumAge) {
    std::lock_guard lock(failureMutex);
    if (!lastFailure || std::chrono::steady_clock::now() - lastFailure->capturedAt > maximumAge)
        return std::nullopt;
    auto result = std::move(lastFailure->failure);
    lastFailure.reset();
    return result;
}

void clearStreamProbe() {
    activeTrace = {};
    std::lock_guard lock(failureMutex);
    lastFailure.reset();
}

} // namespace dobby
