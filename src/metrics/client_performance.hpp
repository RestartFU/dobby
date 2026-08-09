#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace dobby {

struct ClientPerformanceSnapshot {
    std::optional<double> framesPerSecond;
    std::optional<std::uint64_t> residentBytes;
};

class ClientPerformanceTracker {
public:
    // Returns true when a new resident-memory sample is due.
    bool recordPresentation(std::uint64_t nowMicroseconds);
    void recordResidentBytes(std::optional<std::uint64_t> bytes);
    ClientPerformanceSnapshot snapshot(std::uint64_t nowMicroseconds) const;
    std::size_t retainedPresentationSamples() const;

private:
    std::deque<std::uint64_t> presentationMicroseconds_;
    std::optional<std::uint64_t> residentBytes_;
    std::uint64_t nextResidentSampleMicroseconds_{};
};

// Records the current swap and returns a snapshot for the developer overlay.
ClientPerformanceSnapshot captureClientPerformance();

} // namespace dobby
