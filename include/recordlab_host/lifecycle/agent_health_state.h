#pragma once

#include <string>

namespace recordlab::host {

/// Health states for the Watchdog state machine (PLAN.md §3.1 T1).
enum class AgentHealthState {
    DISCONNECTED,   ///< No agent connected, periodic check attempts.
    INITIALIZING,   ///< Agent connected, waiting for init_device to complete.
    HEALTHY,        ///< Agent fully initialized and operational.
};

/// Human-readable name for the state.
inline std::string to_string(AgentHealthState s) {
    switch (s) {
    case AgentHealthState::DISCONNECTED:  return "DISCONNECTED";
    case AgentHealthState::INITIALIZING:  return "INITIALIZING";
    case AgentHealthState::HEALTHY:       return "HEALTHY";
    }
    return "UNKNOWN";
}

}  // namespace recordlab::host