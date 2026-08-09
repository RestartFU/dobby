#include "core/config.hpp"
#include "core/constants.hpp"
#include "core/preferences.hpp"
#include "core/runtime_state.hpp"
#include "diagnostics/client_schema_trace.hpp"
#include "diagnostics/report_builder.hpp"
#include "diagnostics/stream_probe.hpp"
#include "diagnostics/violation_decoder.hpp"
#include "metrics/chunk_metrics_layout.hpp"
#include "network/packet_names.hpp"
#include "metrics/network_metrics.hpp"
#include "platform/preferences_store.hpp"
#include "ui/entity_hitbox_overlay.hpp"
#include "ui/network_metrics_overlay.hpp"
#include "ui/window_policy.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <string>

namespace {

void require(bool condition) {
    if (!condition)
        std::abort();
}

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
    assert(diagnostic.report.find("Decode: ReadOnlyBinaryStream::read") != std::string::npos);
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
    assert(correlatedDiagnostic.report.find("Decode boundary: unconfirmed") != std::string::npos);
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
    assert(trailingDiagnostic.report.find("Decode: ReadOnlyBinaryStream::ensureReadCompleted") != std::string::npos);
    assert(trailingDiagnostic.report.find("_read: success | cursor 4/6 | remaining 2 | overflow no") != std::string::npos);
    assert(trailingDiagnostic.json.find("\"kind\":\"unconsumed_trailing_bytes\"") != std::string::npos);
    assert(trailingDiagnostic.report.find("INFER") == std::string::npos);
    assert(trailingDiagnostic.report.find("Likely") == std::string::npos);
    assert(trailingDiagnostic.json.find("expected_schema") == std::string::npos);
    assert(trailingDiagnostic.json.find("inferred_divergence") == std::string::npos);
}

void testClientSchemaFieldTrace() {
    const std::array<std::uint8_t, 2> body{0xaa, 0xbb};
    std::array<std::byte, 0x48> stream{};
    store<const std::uint8_t*>(stream.data() + dobby::kStreamViewDataOffset, body.data());
    store<std::size_t>(stream.data() + dobby::kStreamViewSizeOffset, body.size());

    dobby::clearStreamProbe();
    dobby::pushClientSchemaMember("item");
    dobby::pushClientSchemaMember("stackNetworkId");
    dobby::captureStreamReadAttempt(stream.data(), 1, 2048);
    dobby::popClientSchemaContext();
    dobby::popClientSchemaContext();
    store<std::size_t>(stream.data() + dobby::kStreamReadPointerOffset, 1);
    dobby::captureStreamReadAttempt(stream.data(), 2, 2048);

    auto failure = dobby::recentStreamFailure(std::chrono::seconds(1));
    assert(failure);
    assert(failure->attempts.front().clientField == "item.stackNetworkId");
    assert(failure->attempts.back().clientField.empty());
    auto diagnostic = dobby::buildDiagnostic(
            {0, 2, 50, "read incomplete", "short"}, std::move(failure), "unit test");
    assert(diagnostic.json.find("\"client_field\":\"item.stackNetworkId\"") != std::string::npos);
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

void testEntityHitboxState() {
    auto& state = dobby::runtimeState();
    state.setEntityHitboxesAvailable(false);
    state.setEntityHitboxes(false);
    assert(!state.entityHitboxesAvailable());
    assert(!state.entityHitboxes());
    state.setEntityHitboxesAvailable(true);
    state.setEntityHitboxes(true);
    assert(state.entityHitboxesAvailable());
    assert(state.entityHitboxes());
    state.setEntityHitboxes(false);
    const bool metricsInitiallyVisible = state.networkMetricsOverlay();
    assert(state.toggleNetworkMetricsOverlay() != metricsInitiallyVisible);
    assert(state.toggleNetworkMetricsOverlay() == metricsInitiallyVisible);
    static_cast<void>(metricsInitiallyVisible);
}

void testEntityProjection() {
    static_assert(sizeof(dobby::EntityAabb) == 24);
    static_assert(dobby::target::kActorGetAabbOffset == 0x0ec86fb4);
    static_assert(dobby::target::kActorGetAabbSignature[1] == 0x08);
    static_assert(dobby::target::kCameraProjectionStackOffset == 0x90);
    static_assert(dobby::target::kCameraRightOffset == 0x118);
    static_assert(dobby::target::kCameraPositionOffset == 0x13c);
    static_assert(dobby::target::kViewMatrixGetterOffset == 0x0a5d8ba4);
    static_assert(dobby::target::kCameraPositionGetterOffset == 0x0a5d8b70);
    static_assert(dobby::target::kActorLevelOffset == 0x1d0);
    static_assert(dobby::target::kActorGetLevelOffset == 0x0eca7920);
    static_assert(dobby::target::kLevelGetRuntimeActorListOffset == 0x0f226d10);
    static_assert(dobby::target::kLevelGetRuntimeActorListVtableSlot == 326);
    static_assert(dobby::target::kLevelForEachPlayerOffset == 0x0f225e4c);
    static_assert(dobby::target::kLevelForEachPlayerVtableSlot == 223);
    static_assert(dobby::target::kLevelGetPrimaryLocalPlayerOffset == 0x0f225818);
    static_assert(dobby::target::kLevelGetPrimaryLocalPlayerVtableSlot == 77);
    static_assert(dobby::target::kClientLevelVtableOffset == 0x11ed28b0);
    require(dobby::entityHitboxObservedForPresentation(1, 0));
    require(dobby::entityHitboxObservedForPresentation(25, 24));
    require(!dobby::entityHitboxObservedForPresentation(2, 0));
    require(!dobby::entityHitboxObservedForPresentation(24, 25));
    const dobby::CameraFrame camera{
            {0.0F, 0.0F, 0.0F},
            {{1.0F, 0.0F, 0.0F, 0.0F,
              0.0F, 1.0F, 0.0F, 0.0F,
              0.0F, 0.0F, 1.0F, 0.0F,
              0.0F, 0.0F, 0.0F, 1.0F}},
            {{0.5625F, 0.0F, 0.0F, 0.0F,
              0.0F, 1.0F, 0.0F, 0.0F,
              0.0F, 0.0F, -1.0F, -1.0F,
              0.0F, 0.0F, -0.2F, 0.0F}},
    };
    dobby::ScreenPoint center{};
    const bool centerProjected = dobby::projectWorldPoint(
            camera, {0.0F, 0.0F, -5.0F}, 1920.0F, 1080.0F, center);
    assert(centerProjected);
    assert(center.x == 960.0F);
    assert(center.y == 540.0F);

    dobby::ScreenPoint right{};
    const bool rightProjected = dobby::projectWorldPoint(
            camera, {1.0F, 0.0F, -5.0F}, 1920.0F, 1080.0F, right);
    assert(rightProjected);
    assert(right.x > center.x);
    dobby::ScreenPoint behind{};
    const bool behindProjected = dobby::projectWorldPoint(
            camera, {0.0F, 0.0F, 1.0F}, 1920.0F, 1080.0F, behind);
    assert(!behindProjected);
    const float projection[16]{
            0.5625F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, -1.0F, -1.0F,
            0.0F, 0.0F, -0.2F, 0.0F,
    };
    static_cast<void>(centerProjected);
    static_cast<void>(rightProjected);
    static_cast<void>(behindProjected);
    static_cast<void>(center);
    static_cast<void>(right);
    static_cast<void>(behind);
    static_cast<void>(projection);
}

void testNetworkMetrics() {
    dobby::NetworkMetricsTracker metrics;
    metrics.recordPing(48, 42, 1000);
    auto snapshot = metrics.snapshot(1000);
    assert(snapshot.connected);
    assert(snapshot.pingMilliseconds == 42);
    assert(!snapshot.observedTicksPerSecond);

    for (std::uint64_t elapsed = 0; elapsed <= 1500; elapsed += 100)
        metrics.recordServerTick(100 + elapsed / 50, 1000 + elapsed);
    metrics.recordChunk(1800);
    metrics.recordChunk(2400);
    dobby::setOutstandingChunkMetricsAvailable(true);
    metrics.recordSubChunkRequest(12);
    metrics.recordSubChunkResponse(5);
    snapshot = metrics.snapshot(2500);
    assert(snapshot.observedTicksPerSecond);
    assert(*snapshot.observedTicksPerSecond == 20.0);
    assert(snapshot.chunksReceived == 2);
    assert(snapshot.chunksPerSecond == 2);
    assert(snapshot.outstandingSubChunkRequests == 7);

    const auto text = dobby::formatNetworkMetrics(snapshot);
    assert(text.visible);
    assert(text.ping == "PING 42 MS");
    assert(text.observedTps == "TPS~ 20.0");
    assert(text.chunks == "CHUNKS 2 (2/S)");
    assert(text.pending == "PENDING 7");
    const auto geometry = dobby::buildNetworkMetricsGeometry(
            snapshot, 1280.0F, 720.0F);
    assert(!geometry.shadowVertices.empty());
    assert(!geometry.pingVertices.empty());
    assert(!geometry.tpsVertices.empty());
    assert(!geometry.chunkVertices.empty());
    assert(!geometry.pendingVertices.empty());

    assert(!metrics.snapshot(5001).connected);
    metrics.recordServerTick(1, 5100);
    assert(metrics.retainedTickSamples() == 1);
    for (std::uint64_t index = 1; index < 100; ++index)
        metrics.recordServerTick(index + 1, 5100 + index * 50);
    assert(metrics.retainedTickSamples() <= 64);

    metrics.reset();
    metrics.recordPing(-1, -1, 9000);
    assert(!metrics.snapshot(9000).connected);
    const auto hidden = dobby::formatNetworkMetrics(metrics.snapshot(9000));
    assert(!hidden.visible);

    static_assert(dobby::target::kLevelGetCurrentServerTickOffset == 0x09ad9014);
    static_assert(dobby::target::kLevelGetCurrentServerTickVtableSlot == 81);
    static_assert(dobby::target::kRakNetPeerUpdateOffset == 0x0c2bda48);
    static_assert(dobby::target::kRakNetPeerLastPingOffset == 0x104);
    static_assert(dobby::target::kRakNetPeerAveragePingOffset == 0x108);
    static_assert(dobby::target::kLevelChunkDispatcherOffset == 0x0c2b88e4);
    static_assert(dobby::target::kLevelChunkDispatcherVtableSlotOffset == 0x1209f3c0);
    static_assert(dobby::target::kSubChunkDispatcherOffset == 0x0c2bb704);
    static_assert(dobby::target::kSubChunkDispatcherVtableSlotOffset == 0x120a3080);
    static_assert(dobby::target::kLoopbackSendOffset == 0x0c2de4a4);
    static_assert(dobby::target::kLoopbackSendVtableSlotOffset == 0x120a55a8);
    static_assert(dobby::target::kSubChunkRequestVectorBeginOffset == 0x38);
    static_assert(dobby::target::kSubChunkRequestVectorEndOffset == 0x40);
    static_assert(dobby::target::kSubChunkPositionSize == 12);
    static_assert(dobby::target::kSubChunkPacketDataSize == 576);

    assert(dobby::boundedVectorElementCount(0, 0, 12, 4096) == 0);
    assert(dobby::boundedVectorElementCount(0x1000, 0x1030, 12, 4096) == 4);
    assert(!dobby::boundedVectorElementCount(0x1000, 0x102f, 12, 4096));
    assert(!dobby::boundedVectorElementCount(0x1030, 0x1000, 12, 4096));
    assert(!dobby::boundedVectorElementCount(0, 0x1000, 12, 4096));
    assert(!dobby::boundedVectorElementCount(0x1000, 0x100c, 0, 4096));
    assert(!dobby::boundedVectorElementCount(0x1000, 0xd00c, 12, 4096));
    dobby::setOutstandingChunkMetricsAvailable(false);
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

void testDeveloperPreferences() {
    const dobby::DeveloperPreferences defaults{
            .autoPopup = true,
            .entityHitboxes = true,
            .networkMetricsOverlay = true,
    };

    const auto parsed = dobby::parseDeveloperPreferences(
            "version=1\n"
            "automatic_popup=off\n"
            "entity_hitboxes=false\n"
            "network_metrics=0\n",
            defaults);
    require(!parsed.autoPopup);
    require(!parsed.entityHitboxes);
    require(!parsed.networkMetricsOverlay);

    const auto malformed = dobby::parseDeveloperPreferences(
            "version=1\n"
            "automatic_popup=invalid\n"
            "entity_hitboxes=maybe\n"
            "network_metrics=unknown\n"
            "future_setting=false\n",
            defaults);
    require(malformed.autoPopup);
    require(malformed.entityHitboxes);
    require(malformed.networkMetricsOverlay);

    const auto unsupported = dobby::parseDeveloperPreferences(
            "version=2\nentity_hitboxes=false\n", defaults);
    require(unsupported == defaults);

    const auto serialized = dobby::serializeDeveloperPreferences(parsed);
    const auto roundTrip = dobby::parseDeveloperPreferences(serialized, defaults);
    require(roundTrip == parsed);

    const auto path = std::filesystem::temp_directory_path() /
            "dobby-preferences-test.conf";
    std::error_code error;
    std::filesystem::remove(path, error);
    require(dobby::saveDeveloperPreferencesFile(path.string(), parsed));
    require(dobby::loadDeveloperPreferencesFile(path.string(), defaults) == parsed);
    std::filesystem::remove(path, error);
}

void testDobbyWindowPolicy() {
    constexpr std::uint32_t existing = 1U << 3U;
    assert(dobby::dobbyWindowFlags("Other", existing) == existing);
    const auto flags = dobby::dobbyWindowFlags("Dobby##dobby_violation_v3", existing);
    static_cast<void>(flags);
    assert((flags & (1U << 1U)) != 0);
    assert((flags & (1U << 5U)) != 0);
    assert((flags & (1U << 6U)) != 0);
    assert((flags & (1U << 8U)) != 0);
    assert((flags & existing) != 0);
}

} // namespace

int main() {
    testViolationDecoder();
    testStreamProbeAndReport();
    testClientSchemaFieldTrace();
    testRepeatViolationsAreRetained();
    testEntityHitboxState();
    testEntityProjection();
    testNetworkMetrics();
    testConfigurationAndPacketCatalog();
    testDeveloperPreferences();
    testDobbyWindowPolicy();
    std::cout << "Dobby tests passed\n";
}
