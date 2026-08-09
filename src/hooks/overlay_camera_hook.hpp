#pragma once

namespace dobby {

void installOverlayCameraHook();
bool overlayCameraHookInstalled();
void rememberOverlayLevelIdentity(const void* levelIdentity);

} // namespace dobby
