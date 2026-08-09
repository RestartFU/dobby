#include "diagnostics/report_builder.hpp"

#include "core/config.hpp"
#include "core/constants.hpp"
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
    output << "Primitive read trace (oldest retained -> failure):\n";
    for (std::size_t index = 0; index < failure.attempts.size(); ++index) {
        const auto& attempt = failure.attempts[index];
        output << (attempt.overflow ? "  FAIL " : "  ok   ")
               << '#' << (index + 1)
               << " offset=" << attempt.offset
               << " requested=" << attempt.requested
               << " available=" << attempt.available << '\n';
    }
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

    json +=
            std::string(",\"minecraft_version\":\"") + kMinecraftVersion +
            "\",\"libminecraftpe_build_id\":\"" + kMinecraftBuildId + "\"}";
    return json;
}

std::string buildReport(const Diagnostic& diagnostic) {
    ViolationRecord record{
            diagnostic.type, diagnostic.severity, diagnostic.packetId,
            diagnostic.context, diagnostic.contextStorage};
    std::string report =
            "DOBBY PACKET DIAGNOSTIC\n\n"
            "Captured: " + diagnostic.capturedAt + "\n"
            "Direction: remote server -> this client\n"
            "Intercept: " + diagnostic.intercept + "\n\n"
            "VIOLATION\n"
            "Type: " + violationTypeName(diagnostic.type) +
            " (" + std::to_string(diagnostic.type) + ")\n"
            "Severity: " + severityName(diagnostic.severity) +
            " (" + std::to_string(diagnostic.severity) + ")\n"
            "Packet: " + packetNameString(diagnostic.packetId) +
            " / " + std::to_string(diagnostic.packetId) +
            " / " + packetIdHex(diagnostic.packetId) + "\n"
            "Reason: " + diagnostic.context + "\n\n"
            "EXPECTED WIRE STRUCTURE\n" + std::string(packetWireSchema(diagnostic.packetId)) + "\n\n";

    if (diagnostic.streamFailure) {
        const auto& failure = *diagnostic.streamFailure;
        report +=
                "EXACT DECODE BOUNDARY\n"
                "Decoder stopped at body byte " + std::to_string(failure.failureOffset) +
                " of " + std::to_string(failure.viewSize) + ".\n"
                "It requested " + std::to_string(failure.requested) +
                " byte(s), but only " + std::to_string(failure.available) + " remained.\n"
                "This is the first primitive read that could not be satisfied.\n\n" +
                buildReadTrace(failure) + "\n"
                "RAW READONLYBINARYSTREAM VIEW\n" + buildHexDump(failure) + "\n";
    } else {
        report +=
                "EXACT DECODE BOUNDARY\n"
                "No recent ReadOnlyBinaryStream overflow was available. The stream probe may be unavailable, "
                "the failure may not be a short read, or the warning arrived outside the correlation window.\n\n";
    }

    report +=
            "WARNING PACKET OBJECT LAYOUT\n" + violationObjectLayout(record) + "\n"
            "CLIENT TARGET\n"
            "Dobby: " + kDobbyVersion + "\n"
            "Minecraft Android: " + kMinecraftVersion + "\n"
            "ABI: " + kAbi + "\n"
            "libminecraftpe build ID: " + kMinecraftBuildId + "\n\n"
            "FILES\n"
            "Log: " + logPath() + "\n"
            "JSONL events: " + eventPath() + "\n"
            "Latest: " + latestPath() + "\n\n"
            "RAW JSON\n" + diagnostic.json + "\n";
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
    if (!diagnostic.streamFailure)
        return "Decode boundary unavailable";
    const auto& failure = *diagnostic.streamFailure;
    return
            "Decode failed at byte " + std::to_string(failure.failureOffset) +
            " / " + std::to_string(failure.viewSize) +
            "  -  needed " + std::to_string(failure.requested) +
            ", had " + std::to_string(failure.available);
}

} // namespace dobby
