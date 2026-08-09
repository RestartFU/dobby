#include "core/config.hpp"
#include "core/runtime_state.hpp"
#include "diagnostics/report_builder.hpp"
#include "diagnostics/stream_probe.hpp"
#include "diagnostics/violation_decoder.hpp"
#include "network/packet_names.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

template <class T>
void store(std::byte* destination, T value) {
    std::memcpy(destination, &value, sizeof(value));
}

void testViolationDecoder() {
    std::array<std::byte, 0x80> packet{};
    store<std::int32_t>(packet.data() + dobby::kViolationTypeOffset, 0);
    store<std::int32_t>(packet.data() + dobby::kViolationSeverityOffset, 2);
    store<std::int32_t>(packet.data() + dobby::kViolationPacketIdOffset, 50);

    const std::string shortContext = "BinaryStream read() incomplete";
    packet[dobby::kViolationContextOffset] =
            static_cast<std::byte>(shortContext.size() << 1U);
    std::memcpy(packet.data() + dobby::kViolationContextOffset + 1,
                shortContext.data(), shortContext.size());
    const auto shortRecord = dobby::decodeViolation(packet.data());
    assert(shortRecord);
    assert(shortRecord->type == 0);
    assert(shortRecord->severity == 2);
    assert(shortRecord->packetId == 50);
    assert(shortRecord->context == shortContext);
    assert(shortRecord->contextStorage == "short");

    const std::string longContext(80, 'x');
    packet[dobby::kViolationContextOffset] = std::byte{1};
    store<std::size_t>(packet.data() + dobby::kViolationContextOffset + 8, longContext.size());
    store<const char*>(packet.data() + dobby::kViolationContextOffset + 16, longContext.data());
    const auto longRecord = dobby::decodeViolation(packet.data());
    assert(longRecord);
    assert(longRecord->context == longContext);
    assert(longRecord->contextStorage == "long");
}

void testStreamProbeAndReport() {
    const std::array<std::uint8_t, 6> body{0x01, 0x02, 0x03, 0x04, 0xaa, 0xbb};
    std::array<std::byte, 0x48> stream{};
    store<const std::uint8_t*>(stream.data() + dobby::kStreamViewDataOffset, body.data());
    store<std::size_t>(stream.data() + dobby::kStreamViewSizeOffset, body.size());
    store<std::size_t>(stream.data() + dobby::kStreamReadPointerOffset, 0);
    stream[dobby::kStreamOverflowOffset] = std::byte{0};

    dobby::clearStreamProbe();
    dobby::captureStreamReadAttempt(stream.data(), 2, 2048);
    store<std::size_t>(stream.data() + dobby::kStreamReadPointerOffset, 2);
    dobby::captureStreamReadAttempt(stream.data(), 2, 2048);
    store<std::size_t>(stream.data() + dobby::kStreamReadPointerOffset, 4);
    dobby::captureStreamReadAttempt(stream.data(), 4, 2048);
    auto failure = dobby::recentStreamFailure(std::chrono::seconds(1));
    assert(failure);
    assert(failure->overflowObserved);
    assert(failure->failureOffset == 4);
    assert(failure->viewSize == 6);
    assert(failure->requested == 4);
    assert(failure->available == 2);
    assert(failure->rawBytes.size() == body.size());
    assert(failure->attempts.size() == 3);
    assert(!failure->attempts.front().overflow);
    assert(failure->attempts.back().overflow);

    dobby::ViolationRecord record{0, 2, 50, "read incomplete", "short"};
    auto diagnostic = dobby::buildDiagnostic(record, std::move(failure), "unit test");
    assert(diagnostic.report.find("Decode failure: truncated field at stream byte 4") != std::string::npos);
    assert(diagnostic.report.find("InventorySlot: container_id") != std::string::npos);
    assert(diagnostic.json.find("\"offset\":4") != std::string::npos);
    assert(dobby::rawPacketHex(diagnostic) == "01 02 03 04 aa bb");

    dobby::clearStreamProbe();
    store<std::size_t>(stream.data() + dobby::kStreamReadPointerOffset, 0);
    dobby::captureStreamReadAttempt(stream.data(), 2, 2048);
    store<std::size_t>(stream.data() + dobby::kStreamReadPointerOffset, 2);
    dobby::captureStreamReadAttempt(stream.data(), 2, 2048);
    auto correlated = dobby::recentStreamFailure(std::chrono::seconds(1));
    assert(correlated);
    assert(!correlated->overflowObserved);
    assert(correlated->rawBytes.size() == body.size());
    assert(correlated->attempts.size() == 2);
    auto correlatedDiagnostic = dobby::buildDiagnostic(
            record, std::move(correlated), "unit test");
    assert(correlatedDiagnostic.report.find("Decode boundary: not observed") != std::string::npos);
    assert(correlatedDiagnostic.json.find("\"overflow_observed\":false") != std::string::npos);

    dobby::clearStreamProbe();
    store<std::size_t>(stream.data() + dobby::kStreamReadPointerOffset, 4);
    dobby::capturePacketEndCheck(stream.data(), 2048);
    auto trailing = dobby::recentStreamFailure(std::chrono::seconds(1));
    assert(trailing);
    assert(trailing->packetEndMismatch);
    assert(trailing->failureOffset == 4);
    assert(trailing->available == 2);
    auto trailingDiagnostic = dobby::buildDiagnostic(record, std::move(trailing), "unit test");
    assert(trailingDiagnostic.report.find("Decode failure: unexpected trailing bytes") != std::string::npos);
    assert(trailingDiagnostic.report.find("Client expected: end of InventorySlot at stream byte 4") != std::string::npos);
    assert(trailingDiagnostic.report.find("Server supplied: 2 unexpected byte(s)") != std::string::npos);
    assert(trailingDiagnostic.json.find("\"kind\":\"unexpected_trailing_bytes\"") != std::string::npos);
}

void testRepeatViolationsAreRetained() {
    auto& state = dobby::runtimeState();
    state.clearDiagnostics();
    dobby::Diagnostic first;
    first.packetId = 50;
    first.context = "same failure";
    state.addDiagnostic(first);
    state.addDiagnostic(first);
    const auto snapshot = state.snapshot();
    assert(snapshot.totalViolations == 2);
    assert(snapshot.retainedViolations == 2);
    state.clearDiagnostics();
}

void testInferredTaggedStackNetworkId() {
    const std::array<std::uint8_t, 45> bytes{
            0x32, 0x00, 0x00, 0x00, 0x00, 0xe3, 0x09, 0x01,
            0x00, 0x00, 0x01, 0x00, 0x9e, 0xe1, 0x11, 0x00,
            0x1c, 0xff, 0xff, 0x01, 0x0a, 0x00, 0x00, 0x03,
            0x06, 0x00, 0x44, 0x61, 0x6d, 0x61, 0x67, 0x65,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00};
    dobby::StreamFailure failure;
    failure.viewSize = bytes.size();
    failure.failureOffset = 16;
    failure.available = 29;
    failure.rawBytes.assign(bytes.begin(), bytes.end());
    failure.attempts.push_back({16, 0, 29, false});

    dobby::ViolationRecord record{
            0, 2, 50,
            "BinaryStream read() incomplete\nreadNoHeader failed! packetId: 50", "long"};
    const auto diagnostic = dobby::buildDiagnostic(record, std::move(failure), "unit test");
    assert(diagnostic.inferredDivergence);
    assert(diagnostic.inferredDivergence->offset == 10);
    assert(diagnostic.inferredDivergence->clientVariantValue == -1);
    assert(diagnostic.inferredDivergence->clientLengthValue == 290974);
    assert(diagnostic.inferredDivergence->bytesAfterDeclaredLength == 30);
    assert(diagnostic.report.find("INFERRED DIVERGENCE\nOffset: 0x0A") != std::string::npos);
    assert(diagnostic.report.find("Likely cause: obsolete tagged StackNetworkID encoding") != std::string::npos);
    assert(diagnostic.report.find("01 00 9e e1 11") != std::string::npos);
    assert(diagnostic.report.find("item-user-data length = 290974") != std::string::npos);
    assert(diagnostic.report.find("16->16 (1x0B; zero-length read, not confirmed failure)") != std::string::npos);
    assert(diagnostic.json.find("\"inferred_divergence\":{\"offset\":10") != std::string::npos);
}

void testConfigurationAndPacketCatalog() {
    assert(dobby::parseBoolean("true", false));
    assert(!dobby::parseBoolean("off", true));
    assert(dobby::parseBoolean("invalid", true));
    assert(dobby::parseBoundedSize("0", 100, 1, 1000) == 1);
    assert(dobby::parseBoundedSize("2000", 100, 1, 1000) == 1000);
    assert(dobby::parseBoundedSize("bad", 100, 1, 1000) == 100);

    static_assert(dobby::packetName(50) == "InventorySlot");
    static_assert(dobby::packetName(156) == "PacketViolationWarning");
    static_assert(dobby::packetName(344) == "SyncWorldClocks");
    static_assert(dobby::packetName(9999) == "UnknownPacket");
}

} // namespace

int main() {
    testViolationDecoder();
    testStreamProbeAndReport();
    testRepeatViolationsAreRetained();
    testInferredTaggedStackNetworkId();
    testConfigurationAndPacketCatalog();
    std::cout << "Dobby tests passed\n";
}
