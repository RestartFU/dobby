#include "ui/pixel_text.hpp"

#include <array>
#include <cstddef>

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
    case 'A': return {{14, 17, 17, 31, 17, 17, 17}};
    case 'B': return {{30, 17, 17, 30, 17, 17, 30}};
    case 'C': return {{14, 17, 16, 16, 16, 17, 14}};
    case 'D': return {{30, 17, 17, 17, 17, 17, 30}};
    case 'E': return {{31, 16, 16, 30, 16, 16, 31}};
    case 'F': return {{31, 16, 16, 30, 16, 16, 16}};
    case 'G': return {{14, 17, 16, 23, 17, 17, 15}};
    case 'H': return {{17, 17, 17, 31, 17, 17, 17}};
    case 'I': return {{14, 4, 4, 4, 4, 4, 14}};
    case 'J': return {{7, 2, 2, 2, 18, 18, 12}};
    case 'K': return {{17, 18, 20, 24, 20, 18, 17}};
    case 'L': return {{16, 16, 16, 16, 16, 16, 31}};
    case 'M': return {{17, 27, 21, 21, 17, 17, 17}};
    case 'N': return {{17, 25, 21, 19, 17, 17, 17}};
    case 'O': return {{14, 17, 17, 17, 17, 17, 14}};
    case 'P': return {{30, 17, 17, 30, 16, 16, 16}};
    case 'Q': return {{14, 17, 17, 17, 21, 18, 13}};
    case 'R': return {{30, 17, 17, 30, 20, 18, 17}};
    case 'S': return {{15, 16, 16, 14, 1, 1, 30}};
    case 'T': return {{31, 4, 4, 4, 4, 4, 4}};
    case 'U': return {{17, 17, 17, 17, 17, 17, 14}};
    case 'V': return {{17, 17, 17, 17, 17, 10, 4}};
    case 'W': return {{17, 17, 17, 21, 21, 21, 10}};
    case 'X': return {{17, 17, 10, 4, 10, 17, 17}};
    case 'Y': return {{17, 17, 10, 4, 4, 4, 4}};
    case 'Z': return {{31, 1, 2, 4, 8, 16, 31}};
    case '<': return {{2, 4, 8, 16, 8, 4, 2}};
    case '>': return {{8, 4, 2, 1, 2, 4, 8}};
    case '/': return {{1, 2, 2, 4, 8, 8, 16}};
    case '.': return {{0, 0, 0, 0, 0, 6, 6}};
    case '-': return {{0, 0, 0, 31, 0, 0, 0}};
    case ':': return {{0, 6, 6, 0, 6, 6, 0}};
    case '#': return {{10, 31, 10, 10, 31, 10, 0}};
    default: return {};
    }
}

void appendSegment(std::vector<float>& vertices, float x1, float y1,
                   float x2, float y2, float width, float height) {
    vertices.push_back(x1 / width * 2.0F - 1.0F);
    vertices.push_back(1.0F - y1 / height * 2.0F);
    vertices.push_back(x2 / width * 2.0F - 1.0F);
    vertices.push_back(1.0F - y2 / height * 2.0F);
}

} // namespace

float pixelTextWidth(std::string_view text, float pixel) {
    return text.empty() ? 0.0F
                        : static_cast<float>(text.size() * 6U - 1U) * pixel;
}

void appendPixelText(std::vector<float>& vertices, std::string_view text,
                     float x, float y, float pixel, float surfaceWidth,
                     float surfaceHeight) {
    for (const char character : text) {
        const Glyph rows = glyph(character);
        for (std::size_t row = 0; row < rows.size(); ++row) {
            for (std::size_t column = 0; column < 5; ++column) {
                if ((rows[row] & (1U << (4U - column))) == 0)
                    continue;
                const float left = x + static_cast<float>(column) * pixel;
                const float center =
                        y + (static_cast<float>(row) + 0.5F) * pixel;
                appendSegment(vertices, left, center, left + pixel, center,
                              surfaceWidth, surfaceHeight);
            }
        }
        x += 6.0F * pixel;
    }
}

} // namespace dobby
