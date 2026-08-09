#pragma once

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
};

struct NetworkMetricsGeometry {
    std::vector<float> shadowVertices;
    std::vector<float> pingVertices;
    std::vector<float> tpsVertices;
    std::vector<float> chunkVertices;
    std::vector<float> pendingVertices;
    float lineWidth{1.0F};
};

NetworkMetricsText formatNetworkMetrics(const NetworkMetricsSnapshot& metrics);
NetworkMetricsGeometry buildNetworkMetricsGeometry(
        const NetworkMetricsSnapshot& metrics, float surfaceWidth,
        float surfaceHeight);

} // namespace dobby
