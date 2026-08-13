#include "core/config.hpp"

#include "core/constants.hpp"

#include <charconv>
#include <cstdlib>

namespace dobby {
namespace {

std::string_view environmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view(value);
}

} // namespace

bool parseBoolean(std::string_view value, bool fallback) {
    if (value.empty())
        return fallback;
    if (value == "1" || value == "true" || value == "yes" || value == "on")
        return true;
    if (value == "0" || value == "false" || value == "no" || value == "off")
        return false;
    return fallback;
}

std::size_t parseBoundedSize(
        std::string_view value, std::size_t fallback,
        std::size_t minimum, std::size_t maximum) {
    if (value.empty())
        return fallback;
    std::size_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
        return fallback;
    if (parsed < minimum)
        return minimum;
    return parsed > maximum ? maximum : parsed;
}

Config loadConfig() {
    Config result;
    result.autoPopup = parseBoolean(environmentValue("DOBBY_AUTO_POPUP"), true);
    result.verbose = parseBoolean(environmentValue("DOBBY_VERBOSE"), false);
    result.historyLimit = parseBoundedSize(
            environmentValue("DOBBY_HISTORY_LIMIT"), kDefaultHistoryLimit, 1, kMaximumHistoryLimit);
    result.rawCaptureLimit = parseBoundedSize(
            environmentValue("DOBBY_RAW_CAPTURE_LIMIT"), kDefaultRawCaptureLimit, 64,
            kMaximumRawCaptureLimit);

    const auto overridePath = environmentValue("DOBBY_OUTPUT_DIR");
    if (!overridePath.empty()) {
        result.outputDirectory.assign(overridePath);
    } else {
        const auto home = environmentValue("HOME");
#if defined(DOBBY_HOST_LINUX)
        const auto flatpakId = environmentValue("FLATPAK_ID");
        const auto xdgDataHome = environmentValue("XDG_DATA_HOME");
        if (home.empty()) {
            result.outputDirectory = "/tmp/mcpelauncher";
        } else if (!flatpakId.empty()) {
            result.outputDirectory = std::string(home) + "/data/mcpelauncher";
        } else if (!xdgDataHome.empty()) {
            result.outputDirectory = std::string(xdgDataHome) + "/mcpelauncher";
        } else {
            result.outputDirectory = std::string(home) + "/.local/share/mcpelauncher";
        }
#else
        result.outputDirectory = home.empty()
                ? "/private/tmp"
                : std::string(home) + "/Library/Application Support/mcpelauncher";
#endif
    }
    return result;
}

const Config& config() {
    static const Config value = loadConfig();
    return value;
}

std::string outputPath(std::string_view filename) {
    return config().outputDirectory + "/" + std::string(filename);
}

} // namespace dobby
