#pragma once

#include "metrics/client_performance.hpp"
#include "metrics/network_metrics.hpp"

#include <string>
#include <vector>

namespace dobby {

struct NetworkMetricsText {
    bool visible{};
    std::string ping;
    std::string observedTps;
    std::string chunks;
    std::string pending;
    std::string framesPerSecond;
    std::string residentMemory;
};

struct NetworkMetricsGeometry {
    std::vector<float> shadowVertices;
    std::vector<float> pingVertices;
    std::vector<float> tpsVertices;
    std::vector<float> chunkVertices;
    std::vector<float> pendingVertices;
    std::vector<float> fpsVertices;
    std::vector<float> memoryVertices;
    float lineWidth{1.0F};
};

NetworkMetricsText formatNetworkMetrics(
        const NetworkMetricsSnapshot& metrics,
        const ClientPerformanceSnapshot& performance = {});
NetworkMetricsGeometry buildNetworkMetricsGeometry(
        const NetworkMetricsSnapshot& metrics, float surfaceWidth,
        float surfaceHeight,
        const ClientPerformanceSnapshot& performance = {});

} // namespace dobby
