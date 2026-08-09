#include "ui/developer_ui.hpp"

#if defined(__ANDROID__)

#include "core/constants.hpp"
#include "core/runtime_state.hpp"
#include "diagnostics/report_builder.hpp"
#include "network/packet_names.hpp"
#include "platform/files.hpp"
#include "platform/launcher.hpp"
#include "platform/log.hpp"

#include <array>
#include <string>

namespace dobby {
namespace {

constexpr char kViolationWindowTitle[] = "Dobby##dobby_violation_v3";
constexpr char kStatusWindowTitle[] = "Dobby##dobby_status_v2";

bool menuUnselected(void*) { return false; }
bool autoPopupSelected(void*) { return runtimeState().autoPopup(); }
bool verboseSelected(void*) { return runtimeState().verbose(); }

void windowClosed(void*) { verboseLine("developer window closed"); }

void logCopyResult(bool copied, std::string_view subject) {
    logLine(copied
            ? std::string("UI: copied ") + std::string(subject) + " to clipboard"
            : std::string("UI: clipboard unavailable; wrote ") + clipboardPath());
}

void copyLatestReport(void*) {
    const auto diagnostic = runtimeState().latestDiagnostic();
    if (!diagnostic) {
        logLine("UI: no packet violation is available to copy");
        return;
    }
    logCopyResult(copyToClipboard(diagnostic->report), "full diagnostic");
}

void copyLatestJson(void*) {
    const auto diagnostic = runtimeState().latestDiagnostic();
    if (!diagnostic) {
        logLine("UI: no packet JSON is available to copy");
        return;
    }
    logCopyResult(copyToClipboard(diagnostic->json), "raw JSON");
}

void copyLatestRawPacket(void*) {
    const auto diagnostic = runtimeState().latestDiagnostic();
    if (!diagnostic) {
        logLine("UI: no raw packet capture is available to copy");
        return;
    }
    const std::string raw = rawPacketHex(*diagnostic);
    if (raw.empty()) {
        logLine("UI: the latest violation has no correlated stream capture");
        return;
    }
    logCopyResult(copyToClipboard(raw), "raw packet bytes");
}

void copyStatus(void*) {
    logCopyResult(copyToClipboard(buildDeveloperStatus(runtimeState().snapshot())), "developer status");
}

void clearHistory(void*) {
    runtimeState().clearDiagnostics();
    writeFile(latestPath(), "", "w");
    logLine("UI: cleared Dobby session history");
}

void toggleAutoPopup(void*) {
    const bool enabled = runtimeState().toggleAutoPopup();
    logLine(enabled ? "UI: automatic violation popups enabled"
                    : "UI: automatic violation popups disabled");
}

void toggleVerbose(void*) {
    const bool enabled = runtimeState().toggleVerbose();
    logLine(enabled ? "UI: verbose developer events enabled"
                    : "UI: verbose developer events disabled");
}

} // namespace

void showDeveloperStatus(void*) {
    resolveLauncherApi();
    if (!launcherWindowAvailable()) {
        logLine("ERROR: launcher window API is unavailable");
        return;
    }

    const auto snapshot = runtimeState().snapshot();
    const std::string hook =
            "Hook: " + snapshot.hookStatus + "\n"
            "Warning hook: " + (snapshot.hookInstalled ? "active" : "inactive") +
            "  |  stream probe: " + (snapshot.streamProbeInstalled ? "active" : "inactive");
    const std::string counters =
            "Minecraft " + std::string(kMinecraftVersion) + " / " + kAbi + "\n"
            "Violations: " + std::to_string(snapshot.totalViolations) +
            " total / " + std::to_string(snapshot.retainedViolations) + " retained";
    const std::string toggles =
            std::string("Auto popup: ") + (snapshot.autoPopup ? "on" : "off") +
            "  |  verbose: " + (snapshot.verbose ? "on" : "off");
    std::array<LauncherControl, 6> controls{
            textControl("DOBBY DEVELOPER CLIENT", 2),
            textControl(hook.c_str(), 1),
            textControl(counters.c_str(), 1),
            textControl(toggles.c_str(), 1),
            buttonControl("Copy developer status", copyStatus),
            buttonControl("Clear session history", clearHistory),
    };
    showLauncherWindow(kStatusWindowTitle, controls, windowClosed);
}

void showLatestViolation(void*) {
    resolveLauncherApi();
    if (!launcherWindowAvailable()) {
        logLine("ERROR: launcher window API is unavailable");
        return;
    }

    const auto diagnostic = runtimeState().latestDiagnostic();
    if (!diagnostic) {
        showDeveloperStatus();
        return;
    }

    const std::string packet =
            std::string(packetName(diagnostic->packetId)) + " (" +
            std::to_string(diagnostic->packetId) + " / " + packetIdHex(diagnostic->packetId) + ")";
    const std::string status =
            std::string(violationTypeName(diagnostic->type)) + " / " +
            severityName(diagnostic->severity);
    const std::string boundary = streamFailureSummary(*diagnostic);
    std::array<LauncherControl, 6> controls{
            textControl(packet.c_str(), 2),
            textControl(status.c_str(), 1),
            textControl(diagnostic->context.c_str(), 1),
            textControl(boundary.c_str(), 1),
            buttonControl("Copy report", copyLatestReport),
            buttonControl("Copy raw bytes", copyLatestRawPacket),
    };

    // Non-modal avoids the launcher's persistent full-screen dim layer on close.
    showLauncherWindow(kViolationWindowTitle, controls, windowClosed);
    logLine("UI: displayed packet violation window");
}

void registerDeveloperUi() {
    resolveLauncherApi();
    logLine(launcherWindowAvailable() ? "UI: launcher window API ready"
                                      : "ERROR: launcher window API unavailable");
    logLine(launcherClipboardAvailable() ? "UI: native clipboard ready"
                                         : "ERROR: native clipboard unavailable");
    if (!launcherMenuAvailable()) {
        logLine("ERROR: launcher menu API unavailable");
        return;
    }

    static std::array<LauncherMenuEntry, 8> subentries{
            LauncherMenuEntry{"Developer status", nullptr, menuUnselected, showDeveloperStatus, 0, nullptr},
            LauncherMenuEntry{"Show last violation", nullptr, menuUnselected, showLatestViolation, 0, nullptr},
            LauncherMenuEntry{"Copy diagnostic", nullptr, menuUnselected, copyLatestReport, 0, nullptr},
            LauncherMenuEntry{"Copy raw packet bytes", nullptr, menuUnselected, copyLatestRawPacket, 0, nullptr},
            LauncherMenuEntry{"Copy raw JSON", nullptr, menuUnselected, copyLatestJson, 0, nullptr},
            LauncherMenuEntry{"Clear session history", nullptr, menuUnselected, clearHistory, 0, nullptr},
            LauncherMenuEntry{"Automatic popup", nullptr, autoPopupSelected, toggleAutoPopup, 0, nullptr},
            LauncherMenuEntry{"Verbose developer log", nullptr, verboseSelected, toggleVerbose, 0, nullptr},
    };
    static LauncherMenuEntry root{
            "Dobby", nullptr, menuUnselected, showDeveloperStatus,
            subentries.size(), subentries.data(),
    };
    static std::array<LauncherMenuEntry, 1> roots{root};
    addLauncherMenu(roots);
    logLine("UI: registered Mods > Dobby developer menu");
}

} // namespace dobby
#endif
