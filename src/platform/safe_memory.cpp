#include "platform/safe_memory.hpp"

#include <cstdint>
#include <cstring>

#if defined(__ANDROID__)
#include "platform/launcher.hpp"

#include <mutex>
#endif

namespace dobby {

#if defined(__ANDROID__)
namespace {

using MachTaskSelfFn = std::uint32_t (*)();
using MachVmReadOverwriteFn = int (*)(
        std::uint32_t task, std::uint64_t source, std::uint64_t size,
        std::uint64_t destination, std::uint64_t* copied);

struct HostIoVector {
    void* base{};
    std::size_t size{};
};

using HostGetPidFn = int (*)();
using ProcessVmReadFn = std::ptrdiff_t (*)(
        int pid, const HostIoVector* local, unsigned long localCount,
        const HostIoVector* remote, unsigned long remoteCount,
        unsigned long flags);

struct SafeMemoryApi {
    MachTaskSelfFn machTaskSelf{};
    MachVmReadOverwriteFn machVmReadOverwrite{};
    HostGetPidFn getPid{};
    ProcessVmReadFn processVmRead{};
};

const SafeMemoryApi& safeMemoryApi() {
    static SafeMemoryApi api;
    static std::once_flag resolved;
    std::call_once(resolved, [] {
        api.machTaskSelf = reinterpret_cast<MachTaskSelfFn>(
                resolveHostSymbol("mach_task_self"));
        api.machVmReadOverwrite = reinterpret_cast<MachVmReadOverwriteFn>(
                resolveHostSymbol("mach_vm_read_overwrite"));
        api.getPid = reinterpret_cast<HostGetPidFn>(
                resolveHostSymbol("getpid"));
        api.processVmRead = reinterpret_cast<ProcessVmReadFn>(
                resolveHostSymbol("process_vm_readv"));
    });
    return api;
}

} // namespace
#endif

bool copyReadableMemory(
        const void* source, std::span<std::byte> destination) {
    if (destination.empty())
        return true;
    if (source == nullptr || destination.data() == nullptr)
        return false;

#if defined(__ANDROID__)
    const SafeMemoryApi& api = safeMemoryApi();
    if (api.machTaskSelf != nullptr && api.machVmReadOverwrite != nullptr) {
        std::uint64_t copied = 0;
        const int result = api.machVmReadOverwrite(
                api.machTaskSelf(),
                static_cast<std::uint64_t>(
                        reinterpret_cast<std::uintptr_t>(source)),
                static_cast<std::uint64_t>(destination.size()),
                static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                        destination.data())),
                &copied);
        return result == 0 && copied == destination.size();
    }
    if (api.getPid != nullptr && api.processVmRead != nullptr) {
        HostIoVector local{destination.data(), destination.size()};
        HostIoVector remote{const_cast<void*>(source), destination.size()};
        return api.processVmRead(
                       api.getPid(), &local, 1, &remote, 1, 0) ==
                static_cast<std::ptrdiff_t>(destination.size());
    }
    return false;
#else
    std::memcpy(destination.data(), source, destination.size());
    return true;
#endif
}

} // namespace dobby
