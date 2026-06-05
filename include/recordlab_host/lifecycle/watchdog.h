#pragma once

#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/lifecycle/agent_health_state.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace recordlab::host {

/// Watchdog (PLAN.md T1) — runs in its own worker thread.
/// Monitors agent health via periodic `check` commands and manages
/// the DISCONNECTED → INITIALIZING → HEALTHY / ERROR state machine.
class Watchdog {
public:
    /// Maximum consecutive check failures before declaring DISCONNECTED.
    static constexpr int kMaxCheckFailures = 1;
    /// Interval between check attempts when DISCONNECTED (ms).
    static constexpr int kCheckIntervalDisconnectedMs = 2000;
    /// Interval between checks when HEALTHY (ms).
    static constexpr int kCheckIntervalHealthyMs = 3000;
    /// Maximum init recovery attempts before staying ERROR.
    static constexpr int kMaxInitRetries = 2;

    explicit Watchdog(HostMessageBus& bus);
    Watchdog(HostMessageBus& bus, std::string agent_name);
    ~Watchdog();

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    void start();
    void stop();

    /// Quick health query without blocking.
    AgentHealthState state() const;

    /// Bind the watchdog to an activated agent and start connectivity checks.
    void setActiveAgent(std::string agent_name);

    /// Stop monitoring the active agent without stopping the thread.
    void clearActiveAgent();

    /// Trigger emergency stop → DISCONNECTED.
    void estop();

private:
    void workerLoop();
    AgentHealthState doCheck();
    AgentHealthState doInitDevice();
    bool doRecoveryClose();
    std::string activeAgent() const;
    bool hasActiveAgent() const;
    void publishState(AgentHealthState state, const nlohmann::json& extra = nlohmann::json::object());
    void publishErrorNotification();
    std::optional<HostMessage> waitForResult(const std::string& request_id,
                                             const std::string& cmd,
                                             int timeout_ms);

    HostMessageBus& bus_;
    std::string agent_name_;
    mutable std::mutex agent_mutex_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> estop_requested_{false};
    std::atomic<AgentHealthState> state_{AgentHealthState::DISCONNECTED};
    std::atomic<int> consecutive_failures_{0};
    std::atomic<int> init_failures_{0};
    std::string last_reason_ = "startup";
};

}  // namespace recordlab::host
