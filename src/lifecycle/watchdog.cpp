#include "recordlab_host/lifecycle/watchdog.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <sstream>

namespace recordlab::host {

Watchdog::Watchdog(HostMessageBus& bus, std::string agent_name)
    : bus_(bus), agent_name_(std::move(agent_name)) {
    bus_.registerConsumer(msg::WATCHDOG);
}

Watchdog::~Watchdog() {
    stop();
}

void Watchdog::start() {
    if (running_) return;
    running_ = true;
    estop_requested_ = false;
    state_ = AgentHealthState::DISCONNECTED;
    consecutive_failures_ = 0;
    worker_ = std::thread(&Watchdog::workerLoop, this);
}

void Watchdog::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

AgentHealthState Watchdog::state() const {
    return state_.load();
}

void Watchdog::estop() {
    estop_requested_ = true;
}

// ── Worker thread ──────────────────────────────────────────────

void Watchdog::workerLoop() {
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "started for agent=" + agent_name_);

    while (running_) {
        if (estop_requested_.exchange(false)) {
            common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
                "estop triggered for agent=" + agent_name_);
            state_ = AgentHealthState::DISCONNECTED;
            consecutive_failures_ = 0;
            bus_.publish({
                .source = msg::WATCHDOG, .target = msg::UI, .type = msg::WATCHDOG_STATE,
                .payload = {{"state", ::recordlab::host::to_string(AgentHealthState::DISCONNECTED)}},
            });
            // Publish estop to AgentManager.
            bus_.publish({
                .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::ESTOP,
                .payload = {{"agent_name", agent_name_}},
            });
            continue;
        }

        AgentHealthState current = state_.load();
        AgentHealthState next = current;

        switch (current) {
        case AgentHealthState::DISCONNECTED:
            next = doCheck();
            if (next == AgentHealthState::INITIALIZING) {
                next = doInitDevice();
            }
            break;
        case AgentHealthState::INITIALIZING:
            // Should only be here transiently; fall through to check.
            next = doCheck();
            break;
        case AgentHealthState::HEALTHY:
            next = doCheck();
            if (next == AgentHealthState::DISCONNECTED) {
                // Check failed, fall back.
            }
            break;
        }

        if (next != current) {
            const auto old_s = ::recordlab::host::to_string(current);
            const auto new_s = ::recordlab::host::to_string(next);
            common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
                "state transition: " + old_s + " → " + new_s + " agent=" + agent_name_);
        }
        state_ = next;

        // Notify UI of the new state.
        bus_.publish({
            .source = msg::WATCHDOG, .target = msg::UI, .type = msg::WATCHDOG_STATE,
            .payload = {{"state", ::recordlab::host::to_string(next)}},
        });

        // Wait before next cycle.
        int interval = (next == AgentHealthState::HEALTHY)
            ? kCheckIntervalHealthyMs
            : kCheckIntervalDisconnectedMs;
        for (int i = 0; i < interval && running_; i += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (estop_requested_) break;
        }
    }

    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "stopped for agent=" + agent_name_);
}

AgentHealthState Watchdog::doCheck() {
    common::Logger::instance().log(common::LogLevel::Debug, "Watchdog",
        "sending check for agent=" + agent_name_);

    // Send check command via shared bus to AgentManager.
    bus_.publish({
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::CMD_REQUEST,
        .payload = {{"cmd", "check"}, {"params", nlohmann::json::object()}},
    });

    // Wait for CMD_RESULT from AgentManager (up to 3 seconds).
    auto opt = bus_.waitFor(msg::WATCHDOG, 3000);
    if (!opt || opt->type != msg::CMD_RESULT) {
        consecutive_failures_++;
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check failed for agent=" + agent_name_ +
            " (consecutive=" + std::to_string(consecutive_failures_) + "/"
            + std::to_string(kMaxCheckFailures) + ")");
        if (consecutive_failures_ >= kMaxCheckFailures) {
            return AgentHealthState::DISCONNECTED;
        }
        // Stay in current state for transient failures.
        return state_.load();
    }

    const bool success = opt->payload.value("success", false);
    if (!success) {
        consecutive_failures_++;
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check returned failure for agent=" + agent_name_ +
            " (consecutive=" + std::to_string(consecutive_failures_) + ")");
        if (consecutive_failures_ >= kMaxCheckFailures) {
            return AgentHealthState::DISCONNECTED;
        }
        return state_.load();
    }

    // Check succeeded.
    consecutive_failures_ = 0;
    if (state_.load() == AgentHealthState::DISCONNECTED) {
        return AgentHealthState::INITIALIZING;
    }
    return AgentHealthState::HEALTHY;
}

AgentHealthState Watchdog::doInitDevice() {
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "triggering init_device for agent=" + agent_name_);

    // Tell AgentManager to init_device.
    bus_.publish({
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::INIT_DEVICE,
        .payload = {{"agent_name", agent_name_}},
    });

    // AgentManager will handle init_device and publish CMD_RESULT.
    auto opt = bus_.waitFor(msg::WATCHDOG, 10000);
    if (!opt || opt->type != msg::CMD_RESULT) {
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device timeout/failure for agent=" + agent_name_);
        return AgentHealthState::DISCONNECTED;
    }

    const bool success = opt->payload.value("success", false);
    if (!success) {
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device failed for agent=" + agent_name_);
        return AgentHealthState::DISCONNECTED;
    }

    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "init_device succeeded for agent=" + agent_name_);
    return AgentHealthState::HEALTHY;
}

}  // namespace recordlab::host