#include "ui/packet_traffic_overlay.hpp"

#include "ui/pixel_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace dobby {
namespace {

std::string byteAmount(std::uint64_t bytes, bool perSecond) {
    char value[32]{};
    constexpr double kibibyte = 1024.0;
    constexpr double mebibyte = 1024.0 * kibibyte;
    constexpr double gibibyte = 1024.0 * mebibyte;
    constexpr double tebibyte = 1024.0 * gibibyte;
    const char* suffix = perSecond ? "/S" : "";
    if (static_cast<double>(bytes) >= tebibyte) {
        std::snprintf(value, sizeof(value), "%.1fTB%s",
                      static_cast<double>(bytes) / tebibyte, suffix);
    } else if (static_cast<double>(bytes) >= gibibyte) {
        std::snprintf(value, sizeof(value), "%.1fGB%s",
                      static_cast<double>(bytes) / gibibyte, suffix);
    } else if (static_cast<double>(bytes) >= mebibyte) {
        std::snprintf(value, sizeof(value), "%.1fMB%s",
                      static_cast<double>(bytes) / mebibyte, suffix);
    } else if (bytes >= 1024) {
        std::snprintf(value, sizeof(value), "%.1fKB%s",
                      static_cast<double>(bytes) / kibibyte, suffix);
    } else {
        std::snprintf(value, sizeof(value), "%lluB%s",
                      static_cast<unsigned long long>(bytes), suffix);
    }
    return value;
}

} // namespace

PacketTrafficText formatPacketTraffic(const PacketTrafficSnapshot& traffic) {
    PacketTrafficText result;
    result.incomingSummary =
            "IN " + std::to_string(traffic.incomingPacketsPerSecond) +
            "/S " + byteAmount(traffic.incomingBytesPerSecond, true) +
            " TOTAL " + byteAmount(traffic.incomingBytes, false);
    result.outgoingSummary =
            "OUT " + std::to_string(traffic.outgoingPacketsPerSecond) +
            "/S " + byteAmount(traffic.outgoingBytesPerSecond, true) +
            " TOTAL " + byteAmount(traffic.outgoingBytes, false);
    return result;
}

PacketTrafficGeometry buildPacketTrafficGeometry(
        const PacketTrafficSnapshot& traffic, float surfaceWidth,
        float surfaceHeight) {
    PacketTrafficGeometry result;
    if (!std::isfinite(surfaceWidth) || !std::isfinite(surfaceHeight) ||
        surfaceWidth <= 0.0F || surfaceHeight <= 0.0F) {
        return result;
    }
    const auto text = formatPacketTraffic(traffic);
    const float pixel = std::clamp(
            std::round(surfaceHeight / 540.0F), 1.0F, 3.0F);
    const float margin = 6.0F * pixel;
    const float lineHeight = 9.0F * pixel;
    float longestLine = std::max(
            pixelTextWidth(text.incomingSummary, pixel),
            pixelTextWidth(text.outgoingSummary, pixel));
    const float x = std::max(
            margin, surfaceWidth - margin - longestLine);
    constexpr std::size_t visibleLines = 2;
    const float overlayHeight =
            static_cast<float>(visibleLines) * lineHeight + 3.0F * pixel;
    float y = std::max(
            margin, surfaceHeight - margin - overlayHeight);
    const auto appendLine = [&](std::vector<float>& vertices,
                                std::string_view value) {
        appendPixelText(vertices, value, x, y, pixel,
                        surfaceWidth, surfaceHeight);
        appendPixelText(result.shadowVertices, value, x + pixel, y + pixel,
                        pixel, surfaceWidth, surfaceHeight);
        y += lineHeight;
    };
    appendLine(result.incomingVertices, text.incomingSummary);
    appendLine(result.outgoingVertices, text.outgoingSummary);
    result.lineWidth = pixel;
    return result;
}

} // namespace dobby
