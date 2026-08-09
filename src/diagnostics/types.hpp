#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dobby {

struct InferredDivergence {
    std::size_t offset{};
    std::string likelyCause;
    std::string observedHex;
    std::int64_t intendedSignedValue{};
    std::int64_t clientVariantValue{};
    std::uint64_t clientLengthValue{};
    std::size_t bytesAfterDeclaredLength{};
};

struct ViolationRecord {
    std::int32_t type{};
    std::int32_t severity{};
    std::int32_t packetId{};
    std::string context;
    std::string contextStorage;
};

struct StreamReadAttempt {
    std::size_t offset{};
    std::size_t requested{};
    std::size_t available{};
    bool overflow{};
};

struct StreamFailure {
    bool overflowObserved{};
    bool packetEndMismatch{};
    std::size_t viewSize{};
    std::size_t failureOffset{};
    std::size_t requested{};
    std::size_t available{};
    bool overflowBeforeRead{};
    bool rawBytesTruncated{};
    std::vector<std::uint8_t> rawBytes;
    std::vector<StreamReadAttempt> attempts;
};

struct Diagnostic {
    std::string capturedAt;
    std::int32_t type{};
    std::int32_t severity{};
    std::int32_t packetId{};
    std::string context;
    std::string contextStorage;
    std::string intercept;
    std::optional<StreamFailure> streamFailure;
    std::optional<InferredDivergence> inferredDivergence;
    std::string json;
    std::string report;
};

} // namespace dobby
