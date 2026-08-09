#include "violation_layout.hpp"

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <mutex>
#include <string>
#include <string_view>
#include <time.h>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>

// mcpelauncher-client injects this symbol into each mod's Android linker
// namespace through ANDROID_DLEXT_MCPELAUNCHER_HOOKS. It is intentionally an
// undefined import here; host dlsym() cannot see that private symbol table.
extern "C" void* mcpelauncher_patch(void* address, void* data, std::size_t size)
        __attribute__((weak));

struct LauncherMenuEntry {
    const char* name;
    void* user;
    bool (*selected)(void* user);
    void (*click)(void* user);
    std::size_t length;
    LauncherMenuEntry* subentries;
};

struct LauncherControl {
    int type;
    union Data {
        struct {
            const char* label;
            void* user;
            void (*onClick)(void* user);
        } button;
        struct {
            const char* label;
            int size;
        } text;
        struct {
            const char* label;
            const char* def;
            const char* placeholder;
            void* user;
            void (*onChange)(void* user, const char* value);
        } textinput;
    } data;
};

extern "C" void mcpelauncher_addmenu(std::size_t length, LauncherMenuEntry* entries)
        __attribute__((weak));
extern "C" void mcpelauncher_show_window(
        const char* title, int isModal, void* user, void (*onClose)(void* user),
        int count, LauncherControl* controls) __attribute__((weak));
extern "C" void* mcpelauncher_host_dlopen(const char* path, int mode)
        __attribute__((weak));
extern "C" void* mcpelauncher_host_dlsym(void* handle, const char* symbol)
        __attribute__((weak));
#endif

namespace {

using GetIdFn = std::int32_t (*)(const void* packet);

// Minecraft Android arm64-v8a 1.26.40.5, build ID 5893edc8d56c93cbdb50e0f9436320236b78c89d.
constexpr std::uintptr_t kGetIdOffset = 0x0cfa1cb4;
constexpr std::uintptr_t kGetIdVtableSlotOffset = 0x120f7160;
constexpr std::array<std::uint8_t, 8> kGetIdSignature{0x80, 0x13, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6};

constexpr const char* kTemporaryLogPath = "/private/tmp/packet-debugger.log";
// The hidden suffix gives this compact layout a fresh ImGui persistence key,
// avoiding dimensions saved by older, larger versions of the window.
constexpr const char* kWindowTitle = "Packet rejected##packet_debugger_compact_v2";
constexpr const char* kMinecraftVersion = "1.26.40.5";
constexpr const char* kMinecraftBuildId = "5893edc8d56c93cbdb50e0f9436320236b78c89d";

std::atomic_bool initialized{false};
std::atomic_bool autoPopupEnabled{true};
std::mutex logMutex;
GetIdFn originalGetId = nullptr;
thread_local bool loggingViolation = false;

const std::string& outputDirectory() {
    static const std::string directory = [] {
        if (const char* overridePath = std::getenv("PACKET_DEBUGGER_OUTPUT_DIR");
            overridePath != nullptr && overridePath[0] != '\0') {
            return std::string(overridePath);
        }
        if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
            return std::string(home) + "/Library/Application Support/mcpelauncher";
        }
        return std::string("/private/tmp");
    }();
    return directory;
}

std::string outputPath(std::string_view filename) {
    return outputDirectory() + "/" + std::string(filename);
}

const std::string& logPath() {
    static const std::string path = outputPath("packet-debugger.log");
    return path;
}

const std::string& latestPath() {
    static const std::string path = outputPath("latest-packet-violation.txt");
    return path;
}

const std::string& clipboardFallbackPath() {
    static const std::string path = outputPath("last-copied-diagnostic.txt");
    return path;
}

struct Diagnostic {
    std::string capturedAt;
    std::int32_t type{};
    std::int32_t severity{};
    std::int32_t packetId{};
    std::string context;
    std::string json;
    std::string report;
};

std::mutex diagnosticMutex;
std::vector<Diagnostic> history;
using HostSetClipboardTextFn = void (*)(const char* text);
using AddMenuFn = void (*)(std::size_t length, LauncherMenuEntry* entries);
using ShowWindowFn = void (*)(const char* title, int isModal, void* user,
                              void (*onClose)(void* user), int count,
                              LauncherControl* controls);
HostSetClipboardTextFn hostSetClipboardText = nullptr;
AddMenuFn launcherAddMenu = nullptr;
ShowWindowFn launcherShowWindow = nullptr;

void appendRaw(const char* path, std::string_view text, const char* mode) {
    if (FILE* file = std::fopen(path, mode)) {
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }
}

std::string timestamp() {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    tm local{};
    localtime_r(&now.tv_sec, &local);
    std::array<char, 64> buffer{};
    const auto size = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03ld",
                                    local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                                    local.tm_min, local.tm_sec, now.tv_nsec / 1000000L);
    return std::string(buffer.data(), static_cast<std::size_t>(size > 0 ? size : 0));
}

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (const unsigned char byte : value) {
        switch (byte) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (byte < 0x20U) {
                std::array<char, 7> buffer{};
                std::snprintf(buffer.data(), buffer.size(), "\\u%04x", byte);
                escaped += buffer.data();
            } else {
                escaped += static_cast<char>(byte);
            }
        }
    }
    return escaped;
}

const char* severityName(std::int32_t severity) {
    switch (severity) {
    case 0: return "warning";
    case 1: return "final_warning";
    case 2: return "terminating_connection";
    default: return "unknown";
    }
}

const char* violationTypeName(std::int32_t type) {
    switch (type) {
    case 0: return "PacketMalformed";
    default: return "Unknown";
    }
}

const char* packetName(std::int32_t packetId) {
    switch (packetId) {
    case 49: return "InventoryContent";
    case 50: return "InventorySlot";
    case 51: return "ContainerSetData";
    case 58: return "FullChunkData";
    case 156: return "PacketViolationWarning";
    default: return "UnknownPacket";
    }
}

std::string packetIdHex(std::int32_t packetId) {
    std::array<char, 16> value{};
    std::snprintf(value.data(), value.size(), "0x%x", static_cast<unsigned int>(packetId));
    return value.data();
}

void logLine(std::string_view message) {
    const std::string line = timestamp() + " [packet-debugger] " + std::string(message) + "\n";
    {
        std::lock_guard lock(logMutex);
        appendRaw(logPath().c_str(), line, "a");
        if (logPath() != kTemporaryLogPath)
            appendRaw(kTemporaryLogPath, line, "a");
    }
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, "packet-debugger", std::string(message).c_str());
#endif
}

std::string buildReport(const Diagnostic& diagnostic) {
    return
            "MINECRAFT BEDROCK PACKET VIOLATION\n"
            "\n"
            "Captured: " + diagnostic.capturedAt + "\n"
            "Direction: remote server -> this client\n"
            "Action: connection terminated by Bedrock\n"
            "\n"
            "Violation type: " + violationTypeName(diagnostic.type) +
            " (" + std::to_string(diagnostic.type) + ")\n"
            "Severity: " + severityName(diagnostic.severity) +
            " (" + std::to_string(diagnostic.severity) + ")\n"
            "Offending packet: " + packetName(diagnostic.packetId) + "\n"
            "Packet ID: " + std::to_string(diagnostic.packetId) +
            " (" + packetIdHex(diagnostic.packetId) + ")\n"
            "Intercept: PacketViolationWarningPacket::getId vtable\n"
            "\n"
            "EXACT INTERNAL CONTEXT\n" + diagnostic.context + "\n"
            "\n"
            "CLIENT TARGET\n"
            "Minecraft Android: " + kMinecraftVersion + "\n"
            "ABI: arm64-v8a\n"
            "libminecraftpe build ID: " + kMinecraftBuildId + "\n"
            "\n"
            "FILES\n"
            "History: " + logPath() + "\n"
            "Latest: " + latestPath() + "\n"
            "\n"
            "RAW JSON\n" + diagnostic.json + "\n";
}

#if defined(__ANDROID__)
void resolveLauncherUi() {
    if (launcherAddMenu != nullptr && launcherShowWindow != nullptr)
        return;

    if (mcpelauncher_addmenu != nullptr)
        launcherAddMenu = mcpelauncher_addmenu;
    if (mcpelauncher_show_window != nullptr)
        launcherShowWindow = mcpelauncher_show_window;

    void* menuLibrary = dlopen("libmcpelauncher_menu.so", RTLD_NOW | RTLD_NOLOAD);
    if (menuLibrary == nullptr)
        menuLibrary = dlopen("libmcpelauncher_menu.so", RTLD_NOW);
    if (menuLibrary == nullptr)
        return;
    if (launcherAddMenu == nullptr)
        launcherAddMenu = reinterpret_cast<AddMenuFn>(dlsym(menuLibrary, "mcpelauncher_addmenu"));
    if (launcherShowWindow == nullptr)
        launcherShowWindow = reinterpret_cast<ShowWindowFn>(dlsym(menuLibrary, "mcpelauncher_show_window"));
}

void resolveClipboard() {
    if (hostSetClipboardText != nullptr || mcpelauncher_host_dlopen == nullptr ||
        mcpelauncher_host_dlsym == nullptr) {
        return;
    }

    void* host = mcpelauncher_host_dlopen(nullptr, RTLD_NOW);
    if (host == nullptr)
        return;

    // Use the launcher's own ImGui clipboard route so the active GameWindow
    // backend (GLFW on this Mac) performs the native pasteboard write.
    hostSetClipboardText = reinterpret_cast<HostSetClipboardTextFn>(
            mcpelauncher_host_dlsym(host, "_ZN5ImGui16SetClipboardTextEPKc"));
}

bool copyText(std::string_view text) {
    appendRaw(clipboardFallbackPath().c_str(), text, "w");
    resolveClipboard();
    if (hostSetClipboardText == nullptr)
        return false;
    const std::string owned(text);
    hostSetClipboardText(owned.c_str());
    return true;
}

void copyLatestReport(void*) {
    std::string value;
    {
        std::lock_guard lock(diagnosticMutex);
        if (!history.empty())
            value = history.back().report;
    }
    if (value.empty()) {
        logLine("UI: no packet violation is available to copy");
        return;
    }
    logLine(copyText(value) ? "UI: copied complete diagnostic to clipboard"
                            : "UI: clipboard API unavailable; wrote last-copied-diagnostic.txt");
}

void copyLatestContext(void*) {
    std::string value;
    {
        std::lock_guard lock(diagnosticMutex);
        if (!history.empty())
            value = history.back().context;
    }
    if (value.empty()) {
        logLine("UI: no packet context is available to copy");
        return;
    }
    logLine(copyText(value) ? "UI: copied exact packet context to clipboard"
                            : "UI: clipboard API unavailable; wrote last-copied-diagnostic.txt");
}

void packetWindowClosed(void*) { logLine("UI: diagnostic popup closed"); }

LauncherControl textControl(const char* text, int size = 0) {
    LauncherControl control{};
    control.type = 3;
    control.data.text.label = text;
    control.data.text.size = size;
    return control;
}

LauncherControl buttonControl(const char* label, void (*callback)(void*)) {
    LauncherControl control{};
    control.type = 0;
    control.data.button.label = label;
    control.data.button.user = nullptr;
    control.data.button.onClick = callback;
    return control;
}

void showCompactDiagnostic(void*) {
    resolveLauncherUi();
    if (launcherShowWindow == nullptr) {
        logLine("ERROR: launcher ImGui window API is unavailable");
        return;
    }

    Diagnostic diagnostic;
    {
        std::lock_guard lock(diagnosticMutex);
        if (history.empty()) {
            logLine("UI: no packet violation is available to show");
            return;
        }
        diagnostic = history.back();
    }

    const std::string packet =
            std::string(packetName(diagnostic.packetId)) + "  -  ID " +
            std::to_string(diagnostic.packetId) + " / " + packetIdHex(diagnostic.packetId);
    const std::string status =
            std::string(violationTypeName(diagnostic.type)) + " (" +
            std::to_string(diagnostic.type) + ")  -  " + severityName(diagnostic.severity) +
            " (" + std::to_string(diagnostic.severity) + ")";
    const std::string reason = "EXACT BEDROCK REASON\n" + diagnostic.context;
    std::array<LauncherControl, 6> controls{
            textControl("SERVER PACKET REJECTED", 2),
            textControl(packet.c_str(), 1),
            textControl(status.c_str(), 1),
            textControl(reason.c_str(), 1),
            buttonControl("Copy everything", copyLatestReport),
            buttonControl("Copy exact reason", copyLatestContext),
    };

    // Non-modal is intentional: the launcher's modal close path can leave its
    // full-screen dim layer active after the window is erased.
    launcherShowWindow(kWindowTitle, 0, nullptr, packetWindowClosed,
                       static_cast<int>(controls.size()), controls.data());
    logLine("UI: displayed compact packet violation modal");
}

bool menuUnselected(void*) { return false; }
bool menuAutoPopupSelected(void*) { return autoPopupEnabled.load(std::memory_order_relaxed); }

void toggleAutoPopup(void*) {
    const bool enabled = !autoPopupEnabled.load(std::memory_order_relaxed);
    autoPopupEnabled.store(enabled, std::memory_order_relaxed);
    logLine(enabled ? "UI: automatic violation popups enabled"
                    : "UI: automatic violation popups disabled");
}

void registerUi() {
    resolveLauncherUi();
    resolveClipboard();
    if (launcherShowWindow == nullptr)
        logLine("ERROR: mcpelauncher_show_window is unavailable");
    else
        logLine("UI: launcher ImGui modal API ready");
    if (hostSetClipboardText == nullptr)
        logLine("ERROR: native clipboard bridge is unavailable");
    else
        logLine("UI: native clipboard bridge ready");

    if (launcherAddMenu == nullptr) {
        logLine("ERROR: mcpelauncher_addmenu is unavailable");
        return;
    }

    static std::array<LauncherMenuEntry, 3> subentries{
            LauncherMenuEntry{"Show last error", nullptr, menuUnselected, showCompactDiagnostic, 0, nullptr},
            LauncherMenuEntry{"Copy everything", nullptr, menuUnselected, copyLatestReport, 0, nullptr},
            LauncherMenuEntry{"Automatic popup", nullptr, menuAutoPopupSelected, toggleAutoPopup, 0, nullptr},
    };
    static LauncherMenuEntry root{
            "Packet Debugger", nullptr, menuUnselected, showCompactDiagnostic,
            subentries.size(), subentries.data(),
    };
    launcherAddMenu(1, &root);
    logLine("UI: registered Mods > Packet Debugger menu");
}
#endif

void logViolation(const char* interceptionPoint, const void* packet) {
    if (loggingViolation)
        return;
    loggingViolation = true;
    const auto record = packet_debugger::decodeViolation(packet);
    if (!record) {
        logLine(std::string("unable to decode PacketViolationWarningPacket at ") + interceptionPoint);
        loggingViolation = false;
        return;
    }

    Diagnostic diagnostic;
    diagnostic.capturedAt = timestamp();
    diagnostic.type = record->type;
    diagnostic.severity = record->severity;
    diagnostic.packetId = record->packetId;
    diagnostic.context = record->context;
    diagnostic.json =
            std::string("{\"event\":\"packet_violation\",\"captured_at\":\"") + diagnostic.capturedAt +
            "\",\"direction\":\"server_to_client\",\"intercept\":\"" + interceptionPoint +
            "\",\"type\":" + std::to_string(record->type) +
            ",\"type_name\":\"" + violationTypeName(record->type) +
            "\",\"severity\":" + std::to_string(record->severity) +
            ",\"severity_name\":\"" + severityName(record->severity) +
            "\",\"packet_id\":" + std::to_string(record->packetId) +
            ",\"packet_id_hex\":\"" + packetIdHex(record->packetId) +
            "\",\"packet_name\":\"" + packetName(record->packetId) +
            "\",\"minecraft_version\":\"" + kMinecraftVersion +
            "\",\"libminecraftpe_build_id\":\"" + kMinecraftBuildId +
            "\",\"context\":\"" + jsonEscape(record->context) + "\"}";
    diagnostic.report = buildReport(diagnostic);
    logLine(diagnostic.json);

    {
        std::lock_guard lock(logMutex);
        appendRaw(latestPath().c_str(), diagnostic.report, "w");
    }

    bool unique = false;
    {
        std::lock_guard lock(diagnosticMutex);
        unique = history.empty() || history.back().type != diagnostic.type ||
                 history.back().severity != diagnostic.severity ||
                 history.back().packetId != diagnostic.packetId ||
                 history.back().context != diagnostic.context;
        if (unique) {
            history.push_back(diagnostic);
            if (history.size() > 20)
                history.erase(history.begin());
        }
    }
#if defined(__ANDROID__)
    if (unique && autoPopupEnabled.load(std::memory_order_relaxed))
        showCompactDiagnostic(nullptr);
#endif
    loggingViolation = false;
}

std::int32_t getIdDetour(const void* packet) {
    logViolation("get_id", packet);
    return originalGetId != nullptr ? originalGetId(packet) : 156;
}

struct MinecraftImage {
    std::uintptr_t base{};
    std::uintptr_t executableBegin{};
    std::uintptr_t executableEnd{};
};

MinecraftImage findMinecraftImage() {
    MinecraftImage result;
    dl_iterate_phdr(
            [](dl_phdr_info* info, std::size_t, void* user) {
                if (info->dlpi_name == nullptr || std::strstr(info->dlpi_name, "libminecraftpe.so") == nullptr)
                    return 0;
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

template <std::size_t Size>
bool matches(const void* address, const std::array<std::uint8_t, Size>& signature) {
    return std::memcmp(address, signature.data(), signature.size()) == 0;
}

void installHooks() {
    const auto image = findMinecraftImage();
    if (image.base == 0 || image.executableBegin == 0) {
        logLine("ERROR: libminecraftpe.so was not found");
        return;
    }

    const auto getIdAddress = image.base + kGetIdOffset;
    if (getIdAddress < image.executableBegin || getIdAddress >= image.executableEnd ||
        !matches(reinterpret_cast<const void*>(getIdAddress), kGetIdSignature)) {
        logLine("ERROR: 1.26.40.5 signatures do not match; refusing unsafe hooks");
        return;
    }

    auto* slot = reinterpret_cast<void**>(image.base + kGetIdVtableSlotOffset);
    if (*slot != reinterpret_cast<void*>(getIdAddress)) {
        logLine("ERROR: PacketViolationWarningPacket vtable signature does not match");
        return;
    }

    originalGetId = reinterpret_cast<GetIdFn>(*slot);
    void* replacement = reinterpret_cast<void*>(getIdDetour);
    if (mcpelauncher_patch == nullptr) {
        originalGetId = nullptr;
        logLine("ERROR: launcher did not resolve the mcpelauncher_patch import");
        return;
    }
    if (mcpelauncher_patch(slot, &replacement, sizeof(replacement)) == nullptr || *slot != replacement) {
        originalGetId = nullptr;
        logLine("ERROR: launcher failed to patch PacketViolationWarningPacket vtable");
        return;
    }

    logLine("installed PacketViolationWarningPacket getId vtable hook");
    logLine("READY: PacketViolationWarningPacket diagnostics active for Minecraft 1.26.40.5");
}

} // namespace

extern "C" [[gnu::visibility("default")]] void mod_preinit() {
    logLine("mod_preinit");
}

extern "C" [[gnu::visibility("default")]] void mod_init() {
    logLine("mod_init");
    if (!initialized.exchange(true)) {
#if defined(__ANDROID__)
        registerUi();
#endif
        installHooks();
    }
}

[[gnu::constructor]] void packetDebuggerLoaded() {
    logLine("library loaded");
}
