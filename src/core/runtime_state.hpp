#pragma once

#include "diagnostics/types.hpp"
#include "core/preferences.hpp"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace dobby {

struct RuntimeSnapshot {
    std::string sessionStartedAt;
    std::string hookStatus;
    bool hookInstalled{};
    bool streamProbeInstalled{};
    bool autoPopup{};
    bool verbose{};
    std::size_t totalViolations{};
    std::size_t retainedViolations{};
};

class RuntimeState {
public:
    RuntimeState();

    void setHookStatus(std::string status, bool warningHook, bool streamProbe);
    void addDiagnostic(Diagnostic diagnostic);
    std::optional<Diagnostic> latestDiagnostic() const;
    void clearDiagnostics();
    RuntimeSnapshot snapshot() const;

    bool autoPopup() const;
    bool toggleAutoPopup();
    bool verbose() const;
    bool toggleVerbose();
    bool entityHitboxes() const;
    void setEntityHitboxes(bool enabled);
    bool entityHitboxesAvailable() const;
    void setEntityHitboxesAvailable(bool available);
    bool chestEsp() const;
    bool toggleChestEsp();
    bool chestEspAvailable() const;
    void setChestEspAvailable(bool available);
    bool networkMetricsOverlay() const;
    bool toggleNetworkMetricsOverlay();
    DeveloperPreferences developerPreferences() const;

private:
    mutable std::mutex mutex_;
    std::vector<Diagnostic> history_;
    std::string sessionStartedAt_;
    std::string hookStatus_{"not initialized"};
    std::atomic_bool warningHookInstalled_{false};
    std::atomic_bool streamProbeInstalled_{false};
    std::atomic_bool autoPopup_{true};
    std::atomic_bool verbose_{false};
    std::atomic_bool entityHitboxes_{true};
    std::atomic_bool entityHitboxesAvailable_{false};
    std::atomic_bool chestEsp_{true};
    std::atomic_bool chestEspAvailable_{false};
    std::atomic_bool networkMetricsOverlay_{true};
    std::atomic_size_t totalViolations_{0};
};

RuntimeState& runtimeState();

} // namespace dobby
