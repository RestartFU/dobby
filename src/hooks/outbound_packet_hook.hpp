#pragma once

#include <cstdint>

namespace dobby {

using OutboundPacketHandler = void (*)(void* packet, std::int32_t packetId);

void installOutboundPacketHook();
bool outboundPacketHookInstalled();
bool registerOutboundPacketHandler(OutboundPacketHandler handler);

} // namespace dobby
