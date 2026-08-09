#include "diagnostics/report_builder.hpp"

#include "core/config.hpp"
#include "core/constants.hpp"
#include "diagnostics/divergence_inference.hpp"
#include "diagnostics/violation_decoder.hpp"
#include "network/packet_names.hpp"
#include "network/packet_schema.hpp"
#include "platform/files.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <sstream>

namespace dobby {
namespace {

std::string packetNameString(std::int32_t packetId) {
    return std::string(packetName(packetId));
}

std::string buildReadTrace(const StreamFailure& failure) {
    std::ostringstream output;
    output << "Reads: ";
    if (failure.attempts.empty()) {
        output << "none\n";
        return output.str();
    }

    std::size_t index = 0;
    while (index < failure.attempts.size()) {
        const auto& first = failure.attempts[index];
        std::size_t count = 1;
        std::size_t end = first.offset + first.requested;
        while (index + count < failure.attempts.size()) {
            const auto& next = failure.attempts[index + count];
            if (first.overflow || next.overflow || next.requested != first.requested ||
                next.offset != end) {
                break;
            }
            end += next.requested;
            ++count;
        }
        if (index != 0)
            output << ", ";
        if (first.overflow) {
            output << "FAIL@" << first.offset << "+" << first.requested;
        } else {
            output << first.offset << "->" << end << " (" << count << "x"
                   << first.requested << "B";
            if (first.requested == 0)
                output << "; zero-length read, not confirmed failure";
            output << ')';
        }
        index += count;
    }
    output << '\n';
    return output.str();
}

std::string buildHexDump(const StreamFailure& failure) {
    std::ostringstream output;
    constexpr std::size_t width = 16;
    for (std::size_t start = 0; start < failure.rawBytes.size(); start += width) {
        const bool failureLine = failure.failureOffset >= start && failure.failureOffset < start + width;
        std::array<char, 24> offset{};
        std::snprintf(offset.data(), offset.size(), "%06zx", start);
        output << (failureLine ? "> " : "  ") << offset.data() << "  ";
        const auto count = std::min(width, failure.rawBytes.size() - start);
        output << hexBytes(std::span<const std::uint8_t>(failure.rawBytes.data() + start, count)) << '\n';
    }
    if (failure.rawBytesTruncated)
        output << "  ... capture truncated; full stream size=" << failure.viewSize << " bytes\n";
    return output.str();
}

std::string buildJson(const Diagnostic& diagnostic) {
    std::string json =
            std::string("{\"tool\":\"dobby\",\"tool_version\":\"") + kDobbyVersion +
            "\",\"event\":\"packet_violation\",\"captured_at\":\"" +
            jsonEscape(diagnostic.capturedAt) +
            "\",\"direction\":\"server_to_client\",\"intercept\":\"" +
            jsonEscape(diagnostic.intercept) +
            "\",\"type\":" + std::to_string(diagnostic.type) +
            ",\"type_name\":\"" + violationTypeName(diagnostic.type) +
            "\",\"severity\":" + std::to_string(diagnostic.severity) +
            ",\"severity_name\":\"" + severityName(diagnostic.severity) +
            "\",\"packet_id\":" + std::to_string(diagnostic.packetId) +
            ",\"packet_id_hex\":\"" + packetIdHex(diagnostic.packetId) +
            "\",\"packet_name\":\"" + jsonEscape(packetNameString(diagnostic.packetId)) +
            "\",\"expected_schema\":\"" + jsonEscape(packetWireSchema(diagnostic.packetId)) +
            "\",\"context\":\"" + jsonEscape(diagnostic.context) +
            "\",\"context_storage\":\"" + diagnostic.contextStorage + "\"";

    if (diagnostic.streamFailure) {
        const auto& failure = *diagnostic.streamFailure;
        json +=
                ",\"decode_failure\":{\"offset\":" + std::to_string(failure.failureOffset) +
                ",\"stream_size\":" + std::to_string(failure.viewSize) +
                ",\"kind\":\"" +
                (failure.packetEndMismatch ? "unexpected_trailing_bytes" :
                 failure.overflowObserved ? "primitive_overflow" : "correlated_trace") +
                "\",\"client_expected\":\"" +
                (failure.packetEndMismatch ? "end_of_packet" : "more_field_data") +
                "\"" +
                ",\"overflow_observed\":" + (failure.overflowObserved ? "true" : "false") +
                ",\"requested\":" + std::to_string(failure.requested) +
                ",\"available\":" + std::to_string(failure.available) +
                ",\"overflow_before_read\":" + (failure.overflowBeforeRead ? "true" : "false") +
                ",\"raw_truncated\":" + (failure.rawBytesTruncated ? "true" : "false") +
                ",\"raw_hex\":\"" + jsonEscape(hexBytes(failure.rawBytes)) +
                "\",\"read_trace\":[";
        for (std::size_t index = 0; index < failure.attempts.size(); ++index) {
            const auto& attempt = failure.attempts[index];
            if (index != 0)
                json += ',';
            json +=
                    std::string("{\"offset\":") + std::to_string(attempt.offset) +
                    ",\"requested\":" + std::to_string(attempt.requested) +
                    ",\"available\":" + std::to_string(attempt.available) +
                    ",\"overflow\":" + (attempt.overflow ? "true" : "false") + "}";
        }
        json += "]}";
    } else {
        json += ",\"decode_failure\":null";
    }

    if (diagnostic.inferredDivergence) {
        const auto& inference = *diagnostic.inferredDivergence;
        json +=
                ",\"inferred_divergence\":{\"offset\":" +
                std::to_string(inference.offset) +
                ",\"likely_cause\":\"" + jsonEscape(inference.likelyCause) +
                "\",\"observed_hex\":\"" + jsonEscape(inference.observedHex) +
                "\",\"intended_signed_value\":" +
                std::to_string(inference.intendedSignedValue) +
                ",\"client_variant_value\":" +
                std::to_string(inference.clientVariantValue) +
                ",\"client_length_value\":" +
                std::to_string(inference.clientLengthValue) +
                ",\"bytes_after_declared_length\":" +
                std::to_string(inference.bytesAfterDeclaredLength) + "}";
    } else {
        json += ",\"inferred_divergence\":null";
    }

    json +=
            std::string(",\"minecraft_version\":\"") + kMinecraftVersion +
            "\",\"libminecraftpe_build_id\":\"" + kMinecraftBuildId + "\"}";
    return json;
}

std::string buildReport(const Diagnostic& diagnostic) {
    std::string report =
            "DOBBY PACKET DIAGNOSTIC\n"
            + diagnostic.capturedAt + " | server -> client\n\n"
            + packetNameString(diagnostic.packetId) + " (" +
            std::to_string(diagnostic.packetId) + " / " +
            packetIdHex(diagnostic.packetId) + ")\n" +
            violationTypeName(diagnostic.type) + " / " + severityName(diagnostic.severity) +
            "\n" + diagnostic.context + "\n\n";

    if (diagnostic.streamFailure) {
        const auto& failure = *diagnostic.streamFailure;
        if (failure.packetEndMismatch) {
            report +=
                    "Decode failure: unexpected trailing bytes\n"
                    "Client expected: end of " + packetNameString(diagnostic.packetId) +
                    " at stream byte " + std::to_string(failure.failureOffset) + "\n"
                    "Server supplied: " + std::to_string(failure.available) +
                    " unexpected byte(s) after that point (" +
                    std::to_string(failure.viewSize) + "B total)\n" +
                    buildReadTrace(failure) +
                    "Expected structure: " +
                    std::string(packetWireSchema(diagnostic.packetId)) + "\n"
                    "Raw packet stream ('>' marks the first unexpected byte):\n" +
                    buildHexDump(failure) + "\n";
        } else if (failure.overflowObserved) {
            report +=
                    "Decode failure: truncated field at stream byte " +
                    std::to_string(failure.failureOffset) + "\n"
                    "Client expected: " + std::to_string(failure.requested) +
                    " byte(s); server supplied " + std::to_string(failure.available) +
                    " byte(s)\n";
            report +=
                    "Expected structure: " +
                    std::string(packetWireSchema(diagnostic.packetId)) + "\n"
                    "Raw packet stream ('>' marks failure):\n" + buildHexDump(failure) + "\n";
        } else {
            report +=
                    "Decode boundary: not observed; last correlated cursor was stream byte " +
                    std::to_string(failure.failureOffset) + "\n" +
                    buildReadTrace(failure) +
                    "Expected structure: " +
                    std::string(packetWireSchema(diagnostic.packetId)) + "\n"
                    "Raw packet stream ('>' marks cursor):\n" + buildHexDump(failure) + "\n";
        }
    } else {
        report +=
                "Decode: no correlated stream trace\n"
                "Expected: " + std::string(packetWireSchema(diagnostic.packetId)) + "\n\n";
    }

    if (diagnostic.inferredDivergence)
        report += formatInferredDivergence(*diagnostic.inferredDivergence) + "\n";

    report +=
            std::string("Dobby ") + kDobbyVersion + " | Minecraft " + kMinecraftVersion +
            " | " + kAbi + "\n";
    return report;
}

} // namespace

const char* severityName(std::int32_t severity) {
    switch (severity) {
    case -1: return "unknown";
    case 0: return "warning";
    case 1: return "final_warning";
    case 2: return "terminating_connection";
    default: return "unrecognized";
    }
}

const char* violationTypeName(std::int32_t type) {
    switch (type) {
    case -1: return "Unknown";
    case 0: return "PacketMalformed";
    default: return "Unrecognized";
    }
}

std::string packetIdHex(std::int32_t packetId) {
    std::array<char, 16> value{};
    std::snprintf(value.data(), value.size(), "0x%x", static_cast<unsigned int>(packetId));
    return value.data();
}

Diagnostic buildDiagnostic(
        const ViolationRecord& record, std::optional<StreamFailure> streamFailure,
        std::string intercept) {
    Diagnostic result;
    result.capturedAt = timestamp();
    result.type = record.type;
    result.severity = record.severity;
    result.packetId = record.packetId;
    result.context = record.context;
    result.contextStorage = record.contextStorage;
    result.intercept = std::move(intercept);
    result.streamFailure = std::move(streamFailure);
    if (result.streamFailure)
        result.inferredDivergence = inferDivergence(result.packetId, *result.streamFailure);
    result.json = buildJson(result);
    result.report = buildReport(result);
    return result;
}

std::string buildDeveloperStatus(const RuntimeSnapshot& snapshot) {
    return
            "DOBBY DEVELOPER CLIENT\n"
            "Version: " + std::string(kDobbyVersion) + "\n"
            "Hook: " + snapshot.hookStatus + "\n"
            "Warning hook: " + (snapshot.hookInstalled ? "active" : "inactive") + "\n"
            "Stream probe: " + (snapshot.streamProbeInstalled ? "active" : "inactive") + "\n"
            "Target: Minecraft " + kMinecraftVersion + " / " + kAbi + "\n"
            "Session started: " + snapshot.sessionStartedAt + "\n"
            "Violations: " + std::to_string(snapshot.totalViolations) +
            " total / " + std::to_string(snapshot.retainedViolations) + " retained\n"
            "Auto popup: " + (snapshot.autoPopup ? "on" : "off") +
            "\nVerbose events: " + (snapshot.verbose ? "on" : "off") +
            "\nHistory limit: " + std::to_string(config().historyLimit) +
            "\nRaw capture limit: " + std::to_string(config().rawCaptureLimit) + " bytes\n"
            "Log: " + logPath() + "\n"
            "JSONL: " + eventPath() + "\n";
}

std::string rawPacketHex(const Diagnostic& diagnostic) {
    if (!diagnostic.streamFailure)
        return {};
    return hexBytes(diagnostic.streamFailure->rawBytes);
}

std::string streamFailureSummary(const Diagnostic& diagnostic) {
    if (diagnostic.inferredDivergence)
        return summarizeInferredDivergence(*diagnostic.inferredDivergence);
    if (!diagnostic.streamFailure)
        return "Decode boundary unavailable";
    const auto& failure = *diagnostic.streamFailure;
    if (failure.packetEndMismatch) {
        return
                "Client expected packet end at byte " +
                std::to_string(failure.failureOffset) + "  -  " +
                std::to_string(failure.available) + " unexpected trailing bytes";
    }
    if (failure.overflowObserved) {
        return
                "Decode failed at byte " + std::to_string(failure.failureOffset) +
                " / " + std::to_string(failure.viewSize) +
                "  -  needed " + std::to_string(failure.requested) +
                ", had " + std::to_string(failure.available);
    }
    return
            "Decode trace retained at byte " + std::to_string(failure.failureOffset) +
            " / " + std::to_string(failure.viewSize) + "  -  no overflow observed";
}

} // namespace dobby
