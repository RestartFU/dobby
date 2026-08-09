#pragma once

#include <cstddef>
#include <span>

namespace dobby {

// Copies from the current process without faulting if the source is unmapped.
// Android mods run inside a native launcher host, so the implementation asks
// the host kernel to perform the checked copy rather than dereferencing input.
bool copyReadableMemory(const void* source, std::span<std::byte> destination);

} // namespace dobby
