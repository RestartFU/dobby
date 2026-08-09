#include "platform/log.hpp"

#include "core/constants.hpp"
#include "core/runtime_state.hpp"
#include "platform/files.hpp"

#include <mutex>
#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace dobby {
namespace {

constexpr char kTemporaryLogPath[] = "/private/tmp/dobby.log";
std::mutex logMutex;

} // namespace

void logLine(std::string_view message) {
    const std::string line = timestamp() + " [dobby] " + std::string(message) + "\n";
    {
        std::lock_guard lock(logMutex);
        writeFile(logPath(), line, "a");
        if (logPath() != kTemporaryLogPath)
            writeFile(kTemporaryLogPath, line, "a");
    }
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, "dobby", std::string(message).c_str());
#endif
}

void verboseLine(std::string_view message) {
    if (runtimeState().verbose())
        logLine(std::string("DEV: ") + std::string(message));
}

void recordLifecycleEvent(std::string_view event, std::string_view detail) {
    const std::string json =
            std::string("{\"tool\":\"dobby\",\"tool_version\":\"") + kDobbyVersion +
            "\",\"event\":\"" + jsonEscape(event) +
            "\",\"captured_at\":\"" + timestamp() +
            "\",\"detail\":\"" + jsonEscape(detail) + "\"}\n";
    std::lock_guard lock(logMutex);
    writeFile(eventPath(), json, "a");
}

} // namespace dobby
