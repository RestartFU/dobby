#pragma once

namespace dobby {

void installNetworkMetricsHook();
bool networkPingHookInstalled();
bool serverTickSourceInstalled();
void captureClientServerTick(const void* level);

} // namespace dobby
