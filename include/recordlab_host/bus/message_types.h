#pragma once

/// Host internal message bus type and target constants (PLAN.md §4).
namespace recordlab::host::msg {

// ── Bus targets (consumer queue names) ─────────────────────────
constexpr const char* UI              = "ui";
constexpr const char* AGENT_MANAGER   = "agent_manager";
constexpr const char* SCRIPTS_ACTUATOR = "scripts_actuator";

// ── UI → AgentManager ──────────────────────────────────────────
constexpr const char* ACTIVATE_AGENT  = "activate_agent";   // payload: {agent_name}
constexpr const char* CMD_REQUEST     = "cmd_request";       // payload: {request_id, cmd, params}
constexpr const char* SHUTDOWN_AGENT  = "shutdown_agent";    // payload: {}

// ── AgentManager → UI ──────────────────────────────────────────
constexpr const char* AGENT_ACTIVATED = "agent_activated";   // payload: {agent_name, success, message}
constexpr const char* CMD_RESULT      = "cmd_result";        // payload: {cmd, success, message}
constexpr const char* WATCHDOG_STATE  = "watchdog_state";    // payload: {state}
constexpr const char* LOG_ENTRY       = "log_entry";         // payload: {message}

// ── UI → ScriptsActuator ───────────────────────────────────────
constexpr const char* RUN_SCRIPT      = "run_script";        // payload: {script_path, agent_name}
constexpr const char* STOP_SCRIPT     = "stop_script";       // payload: {}

// ── ScriptsActuator → UI ──────────────────────────────────────
constexpr const char* SCRIPT_OUTPUT   = "script_output";     // payload: {text}
constexpr const char* SCRIPT_FINISHED = "script_finished";   // payload: {exit_code}

// ── DataReceiver → UI (defined here for completeness) ─────────
constexpr const char* TOPIC_DATA      = "topic_data";        // payload: {topic_name, value, frequency_hz}

}  // namespace recordlab::host::msg
