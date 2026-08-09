#pragma once

#include <string_view>

namespace dobby {

void logLine(std::string_view message);
void verboseLine(std::string_view message);
void recordLifecycleEvent(std::string_view event, std::string_view detail);

} // namespace dobby
