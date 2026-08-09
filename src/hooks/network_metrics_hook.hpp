#pragma once

namespace dobby {

void installNetworkMetricsHook();
bool networkPingHookInstalled();
bool serverTickSourceInstalled();
void observeClientLevelForMetrics(const void* level);
void captureObservedClientServerTick();
bool captureClientServerTick(const void* level);

} // namespace dobby
