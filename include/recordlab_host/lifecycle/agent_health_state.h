#pragma once

#include <string>

namespace recordlab::host {

/// Health states for the Watchdog state machine (PLAN.md §3.1 T1).
enum class AgentHealthState {
    DISCONNECTED,   ///< No agent connected, periodic check attempts.
    INITIALIZING,   ///< Agent connected, waiting for init_device to complete.
    HEALTHY,        ///< Agent fully initialized and operational.
    ERROR,          ///< Agent/node is reachable but device state needs manual intervention.
};

/// Human-readable name for the state.
inline std::string to_string(AgentHealthState s) {
    switch (s) {
    case AgentHealthState::DISCONNECTED:  return "DISCONNECTED";
    case AgentHealthState::INITIALIZING:  return "INITIALIZING";
    case AgentHealthState::HEALTHY:       return "HEALTHY";
    case AgentHealthState::ERROR:         return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace recordlab::host
