#pragma once

/// Host internal message bus type and target constants (PLAN.md §4).
namespace recordlab::host::msg {

// ── Bus targets (consumer queue names) ─────────────────────────
constexpr const char* UI              = "ui";
constexpr const char* AGENT_MANAGER   = "agent_manager";
constexpr const char* SCRIPTS_ACTUATOR = "scripts_actuator";
constexpr const char* WATCHDOG        = "watchdog";

// ── UI / lifecycle → AgentManager ──────────────────────────────
constexpr const char* ACTIVATE_AGENT  = "activate_agent";   // payload: {agent_name}
constexpr const char* SHUTDOWN_AGENT  = "shutdown_agent";    // payload: {}

// ── Agent commands → AgentManager ──────────────────────────────
// Shared by UI, Watchdog and future script/master CLI callers.
constexpr const char* CMD_REQUEST     = "cmd_request";       // payload: {request_id, agent_name, cmd, params, priority, silent}
constexpr const char* CMD_RESULT      = "cmd_result";        // payload: {request_id, agent_name, cmd, success, message}

// ── Watchdog → AgentManager ────────────────────────────────────
constexpr const char* INIT_DEVICE     = "init_device_req";   // payload: {request_id, agent_name}
constexpr const char* ESTOP           = "estop";              // payload: {agent_name}

// ── AgentManager → UI ──────────────────────────────────────────
constexpr const char* AGENT_ACTIVATED = "agent_activated";   // payload: {agent_name, success, message}
constexpr const char* WATCHDOG_STATE  = "watchdog_state";    // payload: {agent_name, state, reason, consecutive_failures}
constexpr const char* LOG_ENTRY       = "log_entry";         // payload: {message}

// ── UI → ScriptsActuator ───────────────────────────────────────
constexpr const char* RUN_SCRIPT      = "run_script";        // payload: {script_path, agent_name}
constexpr const char* STOP_SCRIPT     = "stop_script";       // payload: {}

// ── ScriptsActuator → UI ──────────────────────────────────────
constexpr const char* SCRIPT_STARTED  = "script_started";    // payload: {script_id, script_path, agent_name, pid}
constexpr const char* SCRIPT_OUTPUT   = "script_output";     // payload: {text, stream, process, script_path, pid, script_id}
constexpr const char* SCRIPT_FINISHED = "script_finished";   // payload: {script_id, script_path, pid, exit_code}

// ── Process output → UI ───────────────────────────────────────
constexpr const char* PROCESS_OUTPUT  = "process_output";    // payload: {text, stream, process, pid, agent_name, node_name}

// ── DataReceiver → UI (defined here for completeness) ─────────
constexpr const char* TOPIC_DATA      = "topic_data";        // payload: {topic_name, value, frequency_hz}

}  // namespace recordlab::host::msg
