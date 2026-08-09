#include "metrics/client_performance.hpp"

#include "platform/process_memory.hpp"

#include <chrono>
#include <limits>
#include <mutex>

namespace dobby {
namespace {

constexpr std::uint64_t kFpsWindowMicroseconds = 1'000'000;
constexpr std::uint64_t kFpsFreshnessMicroseconds = 250'000;
constexpr std::uint64_t kResidentSampleIntervalMicroseconds = 1'000'000;
constexpr std::size_t kMaximumPresentationSamples = 512;

std::uint64_t monotonicMicroseconds() {
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
}

std::mutex globalPerformanceMutex;
ClientPerformanceTracker globalPerformance;

} // namespace

bool ClientPerformanceTracker::recordPresentation(
        std::uint64_t nowMicroseconds) {
    if (!presentationMicroseconds_.empty() &&
        nowMicroseconds < presentationMicroseconds_.back()) {
        presentationMicroseconds_.clear();
    }
    presentationMicroseconds_.push_back(nowMicroseconds);
    while (presentationMicroseconds_.size() > 2 &&
           nowMicroseconds - presentationMicroseconds_.front() >
                   kFpsWindowMicroseconds) {
        presentationMicroseconds_.pop_front();
    }
    while (presentationMicroseconds_.size() > kMaximumPresentationSamples)
        presentationMicroseconds_.pop_front();

    if (nextResidentSampleMicroseconds_ != 0 &&
        nowMicroseconds < nextResidentSampleMicroseconds_) {
        return false;
    }
    nextResidentSampleMicroseconds_ =
            nowMicroseconds > std::numeric_limits<std::uint64_t>::max() -
                            kResidentSampleIntervalMicroseconds
            ? std::numeric_limits<std::uint64_t>::max()
            : nowMicroseconds + kResidentSampleIntervalMicroseconds;
    return true;
}

void ClientPerformanceTracker::recordResidentBytes(
        std::optional<std::uint64_t> bytes) {
    residentBytes_ = bytes;
}

ClientPerformanceSnapshot ClientPerformanceTracker::snapshot(
        std::uint64_t nowMicroseconds) const {
    ClientPerformanceSnapshot result;
    result.residentBytes = residentBytes_;
    if (presentationMicroseconds_.size() < 2)
        return result;

    const std::uint64_t first = presentationMicroseconds_.front();
    const std::uint64_t last = presentationMicroseconds_.back();
    if (last < first || nowMicroseconds < last ||
        nowMicroseconds - last > kFpsFreshnessMicroseconds || last == first) {
        return result;
    }
    const double elapsedSeconds =
            static_cast<double>(last - first) / 1'000'000.0;
    result.framesPerSecond =
            static_cast<double>(presentationMicroseconds_.size() - 1) /
            elapsedSeconds;
    return result;
}

std::size_t ClientPerformanceTracker::retainedPresentationSamples() const {
    return presentationMicroseconds_.size();
}

ClientPerformanceSnapshot captureClientPerformance() {
    const std::uint64_t now = monotonicMicroseconds();
    bool sampleResidentMemory = false;
    {
        std::lock_guard lock(globalPerformanceMutex);
        sampleResidentMemory = globalPerformance.recordPresentation(now);
    }
    if (sampleResidentMemory) {
        const auto residentBytes = currentProcessResidentBytes();
        std::lock_guard lock(globalPerformanceMutex);
        globalPerformance.recordResidentBytes(residentBytes);
    }
    std::lock_guard lock(globalPerformanceMutex);
    return globalPerformance.snapshot(now);
}

} // namespace dobby
