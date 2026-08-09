#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace dobby {

struct Config {
    bool autoPopup{true};
    bool verbose{false};
    std::size_t historyLimit{};
    std::size_t rawCaptureLimit{};
    std::string outputDirectory;
};

bool parseBoolean(std::string_view value, bool fallback);
std::size_t parseBoundedSize(
        std::string_view value, std::size_t fallback,
        std::size_t minimum, std::size_t maximum);
Config loadConfig();
const Config& config();
std::string outputPath(std::string_view filename);

} // namespace dobby
