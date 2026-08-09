#include "platform/process_memory.hpp"

#if defined(__ANDROID__)
#include "platform/launcher.hpp"

#include <cstdint>
#include <mutex>
#endif

namespace dobby {

#if defined(__ANDROID__)
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
#if defined(__ANDROID__)
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
#else
    return std::nullopt;
#endif
}

} // namespace dobby
