#include "platform/files.hpp"

#include "core/config.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <time.h>

namespace dobby {
namespace {

std::once_flag outputDirectoryOnce;

} // namespace

void ensureOutputDirectory() {
    std::call_once(outputDirectoryOnce, [] {
        std::error_code error;
        std::filesystem::create_directories(config().outputDirectory, error);
    });
}

bool writeFile(std::string_view path, std::string_view text, std::string_view mode) {
    ensureOutputDirectory();
    const std::string ownedPath(path);
    const std::string ownedMode(mode);
    FILE* file = std::fopen(ownedPath.c_str(), ownedMode.c_str());
    if (file == nullptr)
        return false;
    const bool complete = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    std::fclose(file);
    return complete;
}

std::string timestamp() {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    tm local{};
    localtime_r(&now.tv_sec, &local);
    std::array<char, 64> buffer{};
    const auto size = std::snprintf(
            buffer.data(), buffer.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03ld",
            local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
            local.tm_min, local.tm_sec, now.tv_nsec / 1000000L);
    return std::string(buffer.data(), static_cast<std::size_t>(size > 0 ? size : 0));
}

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (const unsigned char byte : value) {
        switch (byte) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (byte < 0x20U) {
                std::array<char, 7> buffer{};
                std::snprintf(buffer.data(), buffer.size(), "\\u%04x", byte);
                escaped += buffer.data();
            } else {
                escaped += static_cast<char>(byte);
            }
        }
    }
    return escaped;
}

std::string hexBytes(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    if (bytes.empty())
        return result;
    result.reserve(bytes.size() * 3 - 1);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0)
            result += ' ';
        result += digits[bytes[index] >> 4U];
        result += digits[bytes[index] & 0x0fU];
    }
    return result;
}

const std::string& logPath() {
    static const std::string value = outputPath("dobby.log");
    return value;
}

const std::string& eventPath() {
    static const std::string value = outputPath("dobby-events.jsonl");
    return value;
}

const std::string& latestPath() {
    static const std::string value = outputPath("latest-dobby-violation.txt");
    return value;
}

const std::string& clipboardPath() {
    static const std::string value = outputPath("dobby-clipboard.txt");
    return value;
}

} // namespace dobby
