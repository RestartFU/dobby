#pragma once

#include <string_view>
#include <vector>

namespace dobby {

float pixelTextWidth(std::string_view text, float pixel);
void appendPixelText(std::vector<float>& vertices, std::string_view text,
                     float x, float y, float pixel, float surfaceWidth,
                     float surfaceHeight);

} // namespace dobby
