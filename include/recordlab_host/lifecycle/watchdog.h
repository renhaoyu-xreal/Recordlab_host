#pragma once

#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/lifecycle/agent_health_state.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace recordlab::host {

/// Watchdog (PLAN.md T1) — runs in its own worker thread.
/// Monitors agent health via periodic `check` commands and manages
/// the DISCONNECTED → INITIALIZING → HEALTHY / ERROR state machine.
class Watchdog {
public:
    /// Maximum consecutive check failures before declaring DISCONNECTED.
    static constexpr int kMaxCheckFailures = 1;
    /// Interval between check attempts when DISCONNECTED (ms).
    static constexpr int kCheckIntervalDisconnectedMs = 1000;
    /// Interval between checks when HEALTHY (ms).
    static constexpr int kCheckIntervalHealthyMs = 1000;
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
    void setActiveAgent(std::string agent_name, bool start_device_after_init = true);

    /// Monitor a generic set of agents for the current script/session.
    void setMonitoredAgents(std::vector<std::string> agent_names, bool script_monitoring = false);

    /// Stop monitoring the active agent without stopping the thread.
    void clearActiveAgent();

    /// Trigger emergency stop → DISCONNECTED.
    void estop();

private:
    void workerLoop();
    AgentHealthState doCheck();
    AgentHealthState doInitDevice();
    AgentHealthState doStartDevice();
    bool doRecoveryReboot();
    bool checkAgent(const std::string& agent_name, int timeout_ms, std::string* failure_reason);
    void sendEstopToMonitoredAgents();
    void sendStopRecordToMonitoredAgents();
    void stopScriptAndStopRecords(const std::string& reason);
    void publishUiLog(const std::string& message,
                      const std::string& level = "info",
                      const std::string& log_type = "watchdog");
    std::string activeAgent() const;
    std::vector<std::string> monitoredAgents() const;
    bool hasActiveAgent() const;
    void publishState(AgentHealthState state, const nlohmann::json& extra = nlohmann::json::object());
    void publishErrorNotification();
    std::optional<HostMessage> waitForResult(const std::string& request_id,
                                             const std::string& cmd,
                                             int timeout_ms);

    HostMessageBus& bus_;
    std::string agent_name_;
    std::vector<std::string> monitored_agent_names_;
    mutable std::mutex agent_mutex_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> estop_requested_{false};
    std::atomic<AgentHealthState> state_{AgentHealthState::DISCONNECTED};
    std::atomic<int> consecutive_failures_{0};
    std::atomic<int> init_failures_{0};
    std::atomic<bool> failure_stop_sent_{false};
    std::atomic<bool> script_monitoring_{false};
    std::atomic<bool> start_pending_{false};
    std::atomic<bool> start_device_after_init_{true};
    std::string last_reason_ = "startup";
};

}  // namespace recordlab::host
