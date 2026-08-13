#include "platform/process_memory.hpp"

#if defined(__ANDROID__) && defined(DOBBY_HOST_DARWIN)
#include "platform/launcher.hpp"

#include <cstdint>
#include <mutex>
#elif defined(DOBBY_HOST_LINUX)
#include <cstdio>
#include <unistd.h>
#endif

namespace dobby {

#if defined(__ANDROID__) && defined(DOBBY_HOST_DARWIN)
namespace {

constexpr int kMachTaskBasicInfoFlavor = 20;

struct HostTimeValue {
    std::int32_t seconds{};
    std::int32_t microseconds{};
};

// Host Darwin ABI for mach_task_basic_info_data_t. The launcher bridge calls
// the native host function, so this layout must match the host rather than the
// Android NDK's declarations.
struct HostMachTaskBasicInfo {
    std::uint64_t virtualSize{};
    std::uint64_t residentSize{};
    std::uint64_t maximumResidentSize{};
    HostTimeValue userTime{};
    HostTimeValue systemTime{};
    std::int32_t policy{};
    std::int32_t suspendCount{};
};

static_assert(sizeof(HostMachTaskBasicInfo) == 48);
constexpr std::uint32_t kMachTaskBasicInfoCount =
        sizeof(HostMachTaskBasicInfo) / sizeof(std::uint32_t);
static_assert(kMachTaskBasicInfoCount == 12);

using MachTaskSelfFn = std::uint32_t (*)();
using TaskInfoFn = int (*)(
        std::uint32_t task, int flavor, std::int32_t* information,
        std::uint32_t* informationCount);

struct ProcessMemoryApi {
    MachTaskSelfFn machTaskSelf{};
    TaskInfoFn taskInfo{};
};

const ProcessMemoryApi& processMemoryApi() {
    static ProcessMemoryApi api;
    static std::once_flag resolved;
    std::call_once(resolved, [] {
        api.machTaskSelf = reinterpret_cast<MachTaskSelfFn>(
                resolveHostSymbol("mach_task_self"));
        api.taskInfo = reinterpret_cast<TaskInfoFn>(
                resolveHostSymbol("task_info"));
    });
    return api;
}

} // namespace
#endif

std::optional<std::uint64_t> currentProcessResidentBytes() {
#if defined(__ANDROID__) && defined(DOBBY_HOST_DARWIN)
    const ProcessMemoryApi& api = processMemoryApi();
    if (api.machTaskSelf == nullptr || api.taskInfo == nullptr)
        return std::nullopt;

    HostMachTaskBasicInfo information{};
    std::uint32_t count = kMachTaskBasicInfoCount;
    const int result = api.taskInfo(
            api.machTaskSelf(), kMachTaskBasicInfoFlavor,
            reinterpret_cast<std::int32_t*>(&information), &count);
    if (result != 0 || count < kMachTaskBasicInfoCount ||
        information.residentSize == 0) {
        return std::nullopt;
    }
    return information.residentSize;
#elif defined(DOBBY_HOST_LINUX)
    FILE* statistics = std::fopen("/proc/self/statm", "r");
    if (statistics == nullptr)
        return std::nullopt;
    unsigned long totalPages{};
    unsigned long residentPages{};
    const int fields = std::fscanf(statistics, "%lu %lu", &totalPages, &residentPages);
    std::fclose(statistics);
    static_cast<void>(totalPages);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (fields != 2 || residentPages == 0 || pageSize <= 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(residentPages) *
            static_cast<std::uint64_t>(pageSize);
#else
    return std::nullopt;
#endif
}

} // namespace dobby
