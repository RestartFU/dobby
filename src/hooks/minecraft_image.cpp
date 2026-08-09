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
                    if (header.p_type != PT_LOAD)
                        continue;
                    const auto begin = image.base + header.p_vaddr;
                    const auto end = begin + header.p_memsz;
                    if (image.loadBegin == 0 || begin < image.loadBegin)
                        image.loadBegin = begin;
                    if (end > image.loadEnd)
                        image.loadEnd = end;
                    if ((header.p_flags & PF_X) != 0) {
                        if (image.executableBegin == 0 || begin < image.executableBegin)
                            image.executableBegin = begin;
                        if (end > image.executableEnd)
                            image.executableEnd = end;
                    }
                }
                return 1;
            },
            &result);
    return result;
}

bool addressIsInImage(const MinecraftImage& image, std::uintptr_t address) {
    return address >= image.loadBegin && address < image.loadEnd;
}

bool addressIsExecutable(const MinecraftImage& image, std::uintptr_t address) {
    return address >= image.executableBegin && address < image.executableEnd;
}

} // namespace dobby
