#pragma once

#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/lifecycle/agent_health_state.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace recordlab::host {

/// Watchdog (PLAN.md T1) — runs in its own worker thread.
/// Monitors agent health via periodic `check` commands and manages
/// the DISCONNECTED → INITIALIZING → HEALTHY state machine.
class Watchdog {
public:
    /// Maximum consecutive check failures before declaring DISCONNECTED.
    static constexpr int kMaxCheckFailures = 3;
    /// Interval between check attempts when DISCONNECTED (ms).
    static constexpr int kCheckIntervalDisconnectedMs = 2000;
    /// Interval between checks when HEALTHY (ms).
    static constexpr int kCheckIntervalHealthyMs = 5000;

    Watchdog(HostMessageBus& bus, std::string agent_name);
    ~Watchdog();

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    void start();
    void stop();

    /// Quick health query without blocking.
    AgentHealthState state() const;

    /// Trigger emergency stop → DISCONNECTED.
    void estop();

private:
    void workerLoop();
    AgentHealthState doCheck();
    AgentHealthState doInitDevice();

    HostMessageBus& bus_;
    std::string agent_name_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> estop_requested_{false};
    std::atomic<AgentHealthState> state_{AgentHealthState::DISCONNECTED};
    std::atomic<int> consecutive_failures_{0};
};

}  // namespace recordlab::host