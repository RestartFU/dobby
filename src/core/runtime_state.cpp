#include "core/runtime_state.hpp"

#include "core/config.hpp"
#include "diagnostics/stream_probe.hpp"
#include "platform/files.hpp"

namespace dobby {

RuntimeState::RuntimeState()
: sessionStartedAt_(timestamp()),
  autoPopup_(config().autoPopup),
  verbose_(config().verbose) {}

void RuntimeState::setHookStatus(std::string status, bool warningHook, bool streamProbe) {
    warningHookInstalled_.store(warningHook, std::memory_order_relaxed);
    streamProbeInstalled_.store(streamProbe, std::memory_order_relaxed);
    std::lock_guard lock(mutex_);
    hookStatus_ = std::move(status);
}

void RuntimeState::addDiagnostic(Diagnostic diagnostic) {
    totalViolations_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(mutex_);
    history_.push_back(std::move(diagnostic));
    if (history_.size() > config().historyLimit)
        history_.erase(history_.begin(), history_.begin() +
                static_cast<std::ptrdiff_t>(history_.size() - config().historyLimit));
}

std::optional<Diagnostic> RuntimeState::latestDiagnostic() const {
    std::lock_guard lock(mutex_);
    if (history_.empty())
        return std::nullopt;
    return history_.back();
}

void RuntimeState::clearDiagnostics() {
    {
        std::lock_guard lock(mutex_);
        history_.clear();
    }
    totalViolations_.store(0, std::memory_order_relaxed);
    clearStreamProbe();
}

RuntimeSnapshot RuntimeState::snapshot() const {
    RuntimeSnapshot result;
    {
        std::lock_guard lock(mutex_);
        result.sessionStartedAt = sessionStartedAt_;
        result.hookStatus = hookStatus_;
        result.retainedViolations = history_.size();
    }
    result.hookInstalled = warningHookInstalled_.load(std::memory_order_relaxed);
    result.streamProbeInstalled = streamProbeInstalled_.load(std::memory_order_relaxed);
    result.autoPopup = autoPopup();
    result.verbose = verbose();
    result.totalViolations = totalViolations_.load(std::memory_order_relaxed);
    return result;
}

bool RuntimeState::autoPopup() const {
    return autoPopup_.load(std::memory_order_relaxed);
}

bool RuntimeState::toggleAutoPopup() {
    return !autoPopup_.exchange(!autoPopup(), std::memory_order_relaxed);
}

bool RuntimeState::verbose() const {
    return verbose_.load(std::memory_order_relaxed);
}

bool RuntimeState::toggleVerbose() {
    return !verbose_.exchange(!verbose(), std::memory_order_relaxed);
}

bool RuntimeState::entityHitboxes() const {
    return entityHitboxes_.load(std::memory_order_relaxed);
}

void RuntimeState::setEntityHitboxes(bool enabled) {
    entityHitboxes_.store(enabled, std::memory_order_relaxed);
}

bool RuntimeState::entityHitboxesAvailable() const {
    return entityHitboxesAvailable_.load(std::memory_order_relaxed);
}

void RuntimeState::setEntityHitboxesAvailable(bool available) {
    entityHitboxesAvailable_.store(available, std::memory_order_relaxed);
}

RuntimeState& runtimeState() {
    static RuntimeState state;
    return state;
}

} // namespace dobby
