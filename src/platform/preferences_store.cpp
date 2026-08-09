#include "platform/preferences_store.hpp"

#include "platform/files.hpp"

namespace dobby {
namespace {

constexpr std::size_t kMaximumPreferencesBytes = 4096;

} // namespace

DeveloperPreferences loadDeveloperPreferences(
        const DeveloperPreferences& fallback) {
    return loadDeveloperPreferencesFile(preferencesPath(), fallback);
}

DeveloperPreferences loadDeveloperPreferencesFile(
        std::string_view path, const DeveloperPreferences& fallback) {
    const auto text = readFile(path, kMaximumPreferencesBytes);
    return text ? parseDeveloperPreferences(*text, fallback) : fallback;
}

bool saveDeveloperPreferences(const DeveloperPreferences& preferences) {
    return saveDeveloperPreferencesFile(preferencesPath(), preferences);
}

bool saveDeveloperPreferencesFile(
        std::string_view path, const DeveloperPreferences& preferences) {
    return writeFileAtomically(path, serializeDeveloperPreferences(preferences));
}

} // namespace dobby
