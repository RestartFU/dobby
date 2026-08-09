#include "ui/network_metrics_overlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace dobby {
namespace {

using Glyph = std::array<unsigned char, 7>;

Glyph glyph(char character) {
    switch (character) {
    case '0': return {{14, 17, 19, 21, 25, 17, 14}};
    case '1': return {{4, 12, 4, 4, 4, 4, 14}};
    case '2': return {{14, 17, 1, 2, 4, 8, 31}};
    case '3': return {{30, 1, 1, 14, 1, 1, 30}};
    case '4': return {{2, 6, 10, 18, 31, 2, 2}};
    case '5': return {{31, 16, 16, 30, 1, 1, 30}};
    case '6': return {{14, 16, 16, 30, 17, 17, 14}};
    case '7': return {{31, 1, 2, 4, 8, 8, 8}};
    case '8': return {{14, 17, 17, 14, 17, 17, 14}};
    case '9': return {{14, 17, 17, 15, 1, 1, 14}};
    case 'C': return {{14, 17, 16, 16, 16, 17, 14}};
    case 'D': return {{30, 17, 17, 17, 17, 17, 30}};
    case 'E': return {{31, 16, 16, 30, 16, 16, 31}};
    case 'G': return {{14, 17, 16, 23, 17, 17, 15}};
    case 'H': return {{17, 17, 17, 31, 17, 17, 17}};
    case 'I': return {{14, 4, 4, 4, 4, 4, 14}};
    case 'M': return {{17, 27, 21, 21, 17, 17, 17}};
    case 'N': return {{17, 25, 21, 19, 17, 17, 17}};
    case 'P': return {{30, 17, 17, 30, 16, 16, 16}};
    case 'K': return {{17, 18, 20, 24, 20, 18, 17}};
    case 'S': return {{15, 16, 16, 14, 1, 1, 30}};
    case 'T': return {{31, 4, 4, 4, 4, 4, 4}};
    case 'U': return {{17, 17, 17, 17, 17, 17, 14}};
    case '(': return {{2, 4, 8, 8, 8, 4, 2}};
    case ')': return {{8, 4, 2, 2, 2, 4, 8}};
    case '/': return {{1, 2, 2, 4, 8, 8, 16}};
    case '.': return {{0, 0, 0, 0, 0, 6, 6}};
    case '-': return {{0, 0, 0, 31, 0, 0, 0}};
    case '~': return {{0, 0, 9, 22, 0, 0, 0}};
    default: return {};
    }
}

float textWidth(std::string_view text, float pixel) {
    if (text.empty())
        return 0.0F;
    return static_cast<float>(text.size() * 6U - 1U) * pixel;
}

void appendSegment(std::vector<float>& vertices, float x1, float y1,
                   float x2, float y2, float width, float height) {
    vertices.push_back(x1 / width * 2.0F - 1.0F);
    vertices.push_back(1.0F - y1 / height * 2.0F);
    vertices.push_back(x2 / width * 2.0F - 1.0F);
    vertices.push_back(1.0F - y2 / height * 2.0F);
}

void appendText(std::vector<float>& vertices, std::string_view text,
                float x, float y, float pixel, float width, float height) {
    for (const char character : text) {
        const Glyph rows = glyph(character);
        for (std::size_t row = 0; row < rows.size(); ++row) {
            for (std::size_t column = 0; column < 5; ++column) {
                if ((rows[row] & (1U << (4U - column))) == 0)
                    continue;
                const float left = x + static_cast<float>(column) * pixel;
                const float center = y + (static_cast<float>(row) + 0.5F) * pixel;
                appendSegment(vertices, left, center, left + pixel, center,
                              width, height);
            }
        }
        x += 6.0F * pixel;
    }
}

} // namespace

NetworkMetricsText formatNetworkMetrics(const NetworkMetricsSnapshot& metrics) {
    NetworkMetricsText result;
    if (!metrics.connected || !metrics.pingMilliseconds)
        return result;
    result.visible = true;
    result.ping = "PING " + std::to_string(*metrics.pingMilliseconds) + " MS";
    if (metrics.observedTicksPerSecond) {
        char value[32]{};
        std::snprintf(value, sizeof(value), "TPS~ %.1f",
                      *metrics.observedTicksPerSecond);
        result.observedTps = value;
    } else {
        result.observedTps = "TPS~ --";
    }
    result.chunks = "CHUNKS " + std::to_string(metrics.loadedChunks) +
            " (" + std::to_string(metrics.chunksPerSecond) + "/S)";
    if (metrics.outstandingSubChunkRequests) {
        result.pending = "PENDING " +
                std::to_string(*metrics.outstandingSubChunkRequests);
    }
    return result;
}

NetworkMetricsGeometry buildNetworkMetricsGeometry(
        const NetworkMetricsSnapshot& metrics, float surfaceWidth,
        float surfaceHeight) {
    NetworkMetricsGeometry result;
    const auto text = formatNetworkMetrics(metrics);
    if (!text.visible || !std::isfinite(surfaceWidth) ||
        !std::isfinite(surfaceHeight) || surfaceWidth <= 0.0F ||
        surfaceHeight <= 0.0F) {
        return result;
    }

    const float pixel = std::clamp(
            std::round(surfaceHeight / 360.0F), 2.0F, 4.0F);
    const float margin = 6.0F * pixel;
    const float lineHeight = 9.0F * pixel;
    const float longest = std::max({
            textWidth(text.ping, pixel), textWidth(text.observedTps, pixel),
            textWidth(text.chunks, pixel), textWidth(text.pending, pixel)});
    const float x = std::max(0.0F, surfaceWidth - margin - longest);
    const float firstY = margin;
    const float secondY = firstY + lineHeight;
    const float thirdY = secondY + lineHeight;
    const float fourthY = thirdY + lineHeight;

    appendText(result.pingVertices, text.ping, x, firstY, pixel,
               surfaceWidth, surfaceHeight);
    appendText(result.tpsVertices, text.observedTps, x, secondY, pixel,
               surfaceWidth, surfaceHeight);
    appendText(result.chunkVertices, text.chunks, x, thirdY, pixel,
               surfaceWidth, surfaceHeight);
    if (!text.pending.empty()) {
        appendText(result.pendingVertices, text.pending, x, fourthY, pixel,
                   surfaceWidth, surfaceHeight);
    }
    appendText(result.shadowVertices, text.ping, x + pixel, firstY + pixel,
               pixel, surfaceWidth, surfaceHeight);
    appendText(result.shadowVertices, text.observedTps, x + pixel,
               secondY + pixel, pixel, surfaceWidth, surfaceHeight);
    appendText(result.shadowVertices, text.chunks, x + pixel,
               thirdY + pixel, pixel, surfaceWidth, surfaceHeight);
    if (!text.pending.empty()) {
        appendText(result.shadowVertices, text.pending, x + pixel,
                   fourthY + pixel, pixel, surfaceWidth, surfaceHeight);
    }
    result.lineWidth = pixel;
    return result;
}

} // namespace dobby
