#include "diagnostics/divergence_inference.hpp"

#include "platform/files.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <sstream>

namespace dobby {
namespace {

struct VarInt {
    std::uint64_t encoded{};
    std::int64_t signedValue{};
    std::size_t size{};
};

std::optional<VarInt> decodeVarInt(std::span<const std::uint8_t> bytes) {
    std::uint64_t value = 0;
    const auto limit = std::min<std::size_t>(bytes.size(), 10);
    for (std::size_t index = 0; index < limit; ++index) {
        const auto byte = bytes[index];
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << (index * 7U);
        if ((byte & 0x80U) == 0) {
            const auto signedValue = static_cast<std::int64_t>(
                    (value >> 1U) ^ (0U - (value & 1U)));
            return VarInt{value, signedValue, index + 1};
        }
    }
    return std::nullopt;
}

std::optional<InferredDivergence>
inferTaggedStackNetworkId(const StreamFailure& failure) {
    const auto& bytes = failure.rawBytes;
    const auto searchEnd = std::min(failure.failureOffset, bytes.size());
    for (std::size_t offset = 0; offset + 4 < searchEnd; ++offset) {
        if (bytes[offset] != 0x01 || bytes[offset + 1] != 0x00)
            continue;
        const auto intended = decodeVarInt(std::span(bytes).subspan(offset + 2));
        if (!intended || intended->size < 2 || offset + 2 + intended->size > searchEnd)
            continue;

        const auto clientVariant = decodeVarInt(std::span(bytes).subspan(offset));
        const auto clientLength = decodeVarInt(std::span(bytes).subspan(offset + 2));
        if (!clientVariant || !clientLength)
            continue;

        const auto observedSize = 2 + intended->size;
        InferredDivergence result;
        result.offset = offset;
        result.likelyCause = "obsolete tagged StackNetworkID encoding";
        result.observedHex = hexBytes(
                std::span<const std::uint8_t>(bytes.data() + offset, observedSize));
        result.intendedSignedValue = intended->signedValue;
        result.clientVariantValue = clientVariant->signedValue;
        result.clientLengthValue = clientLength->encoded;
        result.bytesAfterDeclaredLength = bytes.size() - (offset + observedSize);
        return result;
    }
    return std::nullopt;
}

std::string hexOffset(std::size_t offset) {
    std::array<char, 24> value{};
    std::snprintf(value.data(), value.size(), "0x%02zX", offset);
    return value.data();
}

} // namespace

std::optional<InferredDivergence>
inferDivergence(std::int32_t packetId, const StreamFailure& failure) {
    // StackNetworkID is part of item descriptors carried by both inventory
    // packet families. Requiring the tagged prefix, a multi-byte following
    // VarInt, and a candidate wholly before Bedrock's retained cursor keeps
    // this a narrow inference instead of a byte-pattern claim.
    if (packetId == 49 || packetId == 50)
        return inferTaggedStackNetworkId(failure);
    return std::nullopt;
}

std::string formatInferredDivergence(const InferredDivergence& inference) {
    std::ostringstream output;
    output << "INFERRED DIVERGENCE\n"
           << "Offset: " << hexOffset(inference.offset) << '\n'
           << "Likely cause: " << inference.likelyCause << "\n\n"
           << "Observed:\n  " << inference.observedHex << "\n\n"
           << "Server intended:\n"
           << "  01          hasNetID\n"
           << "  00          variant tag\n"
           << "  " << inference.observedHex.substr(6)
           << "    signed StackNetworkID\n\n"
           << "Client likely decoded:\n"
           << "  01          StackNetworkID variant = " << inference.clientVariantValue << '\n'
           << "  00          BlockRuntimeID = 0\n"
           << "  " << inference.observedHex.substr(6)
           << "    item-user-data length = " << inference.clientLengthValue << '\n'
           << "Available bytes after declared length: "
           << inference.bytesAfterDeclaredLength << "\n";
    return output.str();
}

std::string summarizeInferredDivergence(const InferredDivergence& inference) {
    return "Likely " + inference.likelyCause + " at " + hexOffset(inference.offset);
}

} // namespace dobby
