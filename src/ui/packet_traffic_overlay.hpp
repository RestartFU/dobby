#pragma once

#include "metrics/packet_traffic.hpp"

#include <string>
#include <vector>

namespace dobby {

struct PacketTrafficText {
    std::string incomingSummary;
    std::string outgoingSummary;
};

struct PacketTrafficGeometry {
    std::vector<float> shadowVertices;
    std::vector<float> incomingVertices;
    std::vector<float> outgoingVertices;
    float lineWidth{1.0F};
};

PacketTrafficText formatPacketTraffic(const PacketTrafficSnapshot& traffic);
PacketTrafficGeometry buildPacketTrafficGeometry(
        const PacketTrafficSnapshot& traffic, float surfaceWidth,
        float surfaceHeight);

} // namespace dobby
