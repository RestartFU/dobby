#pragma once

#include <cstdint>
#include <string_view>

namespace dobby {

std::string_view packetWireSchema(std::int32_t packetId);

} // namespace dobby
