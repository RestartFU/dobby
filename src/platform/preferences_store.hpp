#pragma once

#include "core/preferences.hpp"

#include <string_view>

namespace dobby {

DeveloperPreferences loadDeveloperPreferences(
        const DeveloperPreferences& fallback);
bool saveDeveloperPreferences(const DeveloperPreferences& preferences);
DeveloperPreferences loadDeveloperPreferencesFile(
        std::string_view path, const DeveloperPreferences& fallback);
bool saveDeveloperPreferencesFile(
        std::string_view path, const DeveloperPreferences& preferences);

} // namespace dobby
