#pragma once

#include <cstdint>
#include <string_view>

namespace dobby {

std::uint32_t dobbyWindowFlags(std::string_view title, std::uint32_t flags);
bool installDobbyWindowPolicy();
void applyDobbyWindowPolicy(const char* title);

} // namespace dobby
