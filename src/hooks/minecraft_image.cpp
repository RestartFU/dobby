#include "hooks/minecraft_image.hpp"

#include <cstring>
#include <link.h>

namespace dobby {

MinecraftImage findMinecraftImage() {
    MinecraftImage result;
    dl_iterate_phdr(
            [](dl_phdr_info* info, std::size_t, void* user) {
                if (info->dlpi_name == nullptr ||
                    std::strstr(info->dlpi_name, "libminecraftpe.so") == nullptr) {
                    return 0;
                }
                auto& image = *static_cast<MinecraftImage*>(user);
                image.base = static_cast<std::uintptr_t>(info->dlpi_addr);
                for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
                    const auto& header = info->dlpi_phdr[index];
                    if (header.p_type == PT_LOAD && (header.p_flags & PF_X) != 0) {
                        image.executableBegin = image.base + header.p_vaddr;
                        image.executableEnd = image.executableBegin + header.p_memsz;
                        return 1;
                    }
                }
                return 1;
            },
            &result);
    return result;
}

bool addressIsExecutable(const MinecraftImage& image, std::uintptr_t address) {
    return address >= image.executableBegin && address < image.executableEnd;
}

} // namespace dobby
