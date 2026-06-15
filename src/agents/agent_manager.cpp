#include "recordlab_host/agents/agent_manager.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"
#include "recordlab_host/common/process_handle.h"

#include <filesystem>
#include <sstream>
#include <sys/wait.h>

namespace recordlab::host {
namespace {

std::string describeTopics(const std::vector<TopicConfig>& topics, int data_port) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << topics[i].name << "@" << data_port << "/" << topics[i].encoding;
    }
    return oss.str();
}

int commandTimeoutMs(const nlohmann::json& payload) {
    const auto timeout_ms = payload.find("timeout_ms");
    if (timeout_ms != payload.end() && timeout_ms->is_number()) {
        return static_cast<int>(timeout_ms->get<double>());
    }
    const auto timeout_s = payload.find("timeout_s");
    if (timeout_s != payload.end() && timeout_s->is_number()) {
        return static_cast<int>(timeout_s->get<double>() * 1000.0);
    }
    return 0;
}

nlohmann::json makeLogEntry(std::string message,
                            std::string level,
                            std::string log_type) {
    return {
        {"message", std::move(message)},
        {"level", std::move(level)},
        {"log_type", std::move(log_type)},
    };
}

}  // namespace

AgentManager::AgentManager(HostMessageBus& bus, std::string agents_config_path,
                           std::string nodes_root, std::string echo_python_root,
                           std::string python_bin, std::string node_runtime_module)
    : bus_(bus),
      agents_config_path_(std::move(agents_config_path)),
      nodes_root_(std::move(nodes_root)),
      echo_python_root_(std::move(echo_python_root)),
      python_bin_(std::move(python_bin)),
      node_runtime_module_(std::move(node_runtime_module)) {
    bus_.registerConsumer(msg::AGENT_MANAGER);
}

AgentManager::~AgentManager() {
    stop();
}

void AgentManager::start() {
    if (running_) return;
    running_ = true;
    worker_ = std::thread(&AgentManager::workerLoop, this);
}

void AgentManager::stop() {
    if (!running_) return;
    // Push a shutdown message so the worker wakes up and exits.
    bus_.publish({.target = msg::AGENT_MANAGER, .type = msg::SHUTDOWN_AGENT});
    running_ = false;
    if (worker_.joinable()) worker_.join();
    doShutdownAgent();
}

std::vector<std::string> AgentManager::primaryAgents() const {
    return AgentConfigLoader(agents_config_path_).loadPrimaryAgents();
}

AgentConfig AgentManager::loadAgentConfig(const std::string& agent_name) const {
    return AgentConfigLoader(agents_config_path_).loadAgent(agent_name);
}

// ── Worker thread ──────────────────────────────────────────────

void AgentManager::workerLoop() {
    while (running_) {
        auto opt = bus_.waitFor(msg::AGENT_MANAGER, 100);
        if (!opt) continue;
        handleMessage(*opt);
    }
}

void AgentManager::handleMessage(const HostMessage& msg) {
    // Track the source of the current message for CMD_RESULT routing.
    if (!msg.source.empty()) {
        last_source_ = msg.source;
    }
    last_request_id_ = msg.request_id.empty()
        ? msg.payload.value("request_id", std::string{})
        : msg.request_id;

    if (msg.type == msg::SHUTDOWN_AGENT) {
        return;  // workerLoop will exit via running_ flag
    }
    if (msg.type == msg::ACTIVATE_AGENT) {
        const auto agent_name = msg.payload.value("agent_name", std::string{});
        doActivateAgent(agent_name);
    } else if (msg.type == msg::RELEASE_INACTIVE_AGENTS) {
        doReleaseInactiveAgents();
    } else if (msg.type == msg::CMD_REQUEST) {
        const auto agent_name = msg.payload.value("agent_name", active_agent_);
        const auto cmd = msg.payload.value("cmd", std::string{});
        const auto params = msg.payload.value("params", nlohmann::json::object());
        const auto silent = msg.payload.value("silent", false);
        doCmdRequest(agent_name, cmd, params, silent, commandTimeoutMs(msg.payload));
    } else if (msg.type == msg::INIT_DEVICE) {
        const auto agent_name = msg.payload.value("agent_name", active_agent_);
        const std::string target_agent = agent_name.empty() ? active_agent_ : agent_name;
        nlohmann::json params = nlohmann::json::object();
        if (AgentProxy* agent = findAgent(target_agent)) {
            params = agent->config().init_device_params;
        }
        doCmdRequest(agent_name, "init_device", params, false);
    } else if (msg.type == msg::ESTOP) {
        const auto agent_name = msg.payload.value("agent_name", active_agent_);
        doCmdRequest(agent_name, "estop", nlohmann::json::object(), false);
    }
}

void AgentManager::doActivateAgent(const std::string& agent_name) {
    try {
        const auto config = AgentConfigLoader(agents_config_path_).loadAgent(agent_name);
        publishResult(msg::LOG_ENTRY, makeLogEntry("启动 Agent: " + agent_name, "info", "agent"));
        common::Logger::instance().log(
            common::LogLevel::Info,
            "AgentManager",
            "activate agent=" + config.name +
                ", node_class=" + config.node_class +
                ", action=" + config.action_name +
                ", goal_port=" + std::to_string(config.goal_port) +
                ", feedback_port=" + std::to_string(config.feedback_port) +
                ", data_port=" + std::to_string(config.data_port) +
                ", topics=[" + describeTopics(config.topics, config.data_port) + "]",
            {
                {"request_id", last_request_id_},
                {"agent_name", config.name},
                {"node_name", config.name},
                {"data_port", config.data_port},
            });
        if (!active_agent_.empty() && active_agent_ != config.name) {
            if (AgentProxy* current = activeAgent()) current->disconnect();
        }
        AgentProxy& agent = getOrCreateAgent(config);
        if (agent.launchOrConnect()) {
            active_agent_ = config.name;
            publishResult(msg::LOG_ENTRY, makeLogEntry(
                "Agent " + agent_name + " 连接成功，等待 Watchdog 初始化", "success", "agent"));
            publishResult(msg::AGENT_ACTIVATED, {
                {"agent_name", agent_name}, {"success", true},
                {"message", "Agent activated"},
                {"subnode_host", config.subnode_host},
                {"watchdog_start_device", config.watchdog_start_device},
                {"topics", [&]() {
                    nlohmann::json arr = nlohmann::json::array();
                    for (const auto& t : config.topics)
                        arr.push_back({{"name", t.name}, {"port", config.data_port}, {"encoding", t.encoding}});
                    return arr;
                }()},
                {"init_device_params", config.init_device_params},
            });
        } else {
            publishResult(msg::LOG_ENTRY, makeLogEntry("Agent " + agent_name + " 连接失败", "error", "agent"));
            publishResult(msg::AGENT_ACTIVATED, {
                {"agent_name", agent_name}, {"success", false}, {"message", "Connection failed"},
            });
        }
    } catch (const std::exception& e) {
        publishResult(msg::LOG_ENTRY, makeLogEntry(
            std::string("Agent 启动失败: ") + e.what(), "error", "agent"));
        publishResult(msg::AGENT_ACTIVATED, {
            {"agent_name", agent_name}, {"success", false}, {"message", e.what()},
        });
    }
}

void AgentManager::doCmdRequest(const std::string& agent_name, const std::string& cmd,
                                const nlohmann::json& params, bool silent, int timeout_ms) {
    const std::string target_agent = agent_name.empty() ? active_agent_ : agent_name;
    if (target_agent.empty()) {
        publishResult(msg::CMD_RESULT, {
            {"agent_name", target_agent},
            {"cmd", cmd},
            {"success", false},
            {"message", "当前没有 active Agent"},
        });
        return;
    }
    if (!active_agent_.empty() && target_agent != active_agent_) {
        common::Logger::instance().log(common::LogLevel::Debug, "AgentManager",
            "routing command to non-active agent=" + target_agent + ", active=" + active_agent_,
            {{"request_id", last_request_id_}, {"agent_name", target_agent}, {"active_agent", active_agent_}, {"cmd", cmd}});
    }
    AgentConfig config;
    try {
        config = AgentConfigLoader(agents_config_path_).loadAgent(target_agent);
    } catch (const std::exception& e) {
        publishResult(msg::CMD_RESULT, {
            {"agent_name", target_agent},
            {"cmd", cmd},
            {"success", false},
            {"message", e.what()},
        });
        return;
    }
    if (config.process_type == "local_scripts") {
        doLocalScriptCommand(config, cmd, params, silent);
        return;
    }
    AgentProxy* agent = ensureAgentConnected(target_agent, timeout_ms);
    if (!agent || !agent->isConnected()) {
        publishResult(msg::CMD_RESULT, {
            {"agent_name", target_agent},
            {"cmd", cmd},
            {"success", false},
            {"message", "当前没有可用 Agent client"},
        });
        return;
    }
    try {
        if (!silent) {
            publishResult(msg::LOG_ENTRY, makeLogEntry(
                "发送命令: " + cmd + " " + params.dump(), "info", "command"));
        }
        common::Logger::instance().log(common::LogLevel::Info, "AgentManager", "sending command", {
            {"request_id", last_request_id_},
            {"agent_name", target_agent},
            {"cmd", cmd},
        });
        const int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : agent->config().commandTimeoutMs(cmd);
        const auto result = agent->cmd(cmd, params, effective_timeout_ms);
        const auto message = result.result.value("message", result.result.dump());
        common::Logger::instance().log(
            result.success ? common::LogLevel::Info : common::LogLevel::Warn,
            "AgentManager",
            "command result",
            {
                {"request_id", last_request_id_},
                {"agent_name", target_agent},
                {"cmd", cmd},
                {"success", result.success},
                {"message", message},
            });
        publishResult(msg::CMD_RESULT, {
            {"agent_name", target_agent},
            {"cmd", cmd},
            {"success", result.success},
            {"message", message},
            {"result", result.result},
        });
        if (!silent) {
            publishResult(msg::LOG_ENTRY, makeLogEntry(
                cmd + ": " + message,
                result.success ? "success" : "error",
                "command"));
        }
    } catch (const std::exception& e) {
        common::Logger::instance().log(common::LogLevel::Error, "AgentManager", "command exception", {
            {"request_id", last_request_id_},
            {"agent_name", target_agent},
            {"cmd", cmd},
            {"message", e.what()},
        });
        publishResult(msg::CMD_RESULT, {
            {"agent_name", target_agent},
            {"cmd", cmd},
            {"success", false},
            {"message", e.what()},
            {"result", nlohmann::json::object()},
        });
        if (!silent) {
            publishResult(msg::LOG_ENTRY, makeLogEntry(
                cmd + " 失败: " + std::string(e.what()), "error", "command"));
        }
    }
}

void AgentManager::doLocalScriptCommand(const AgentConfig& config, const std::string& cmd,
                                        const nlohmann::json& params, bool silent) {
    if (cmd == "check" || cmd == "estop") {
        publishResult(msg::CMD_RESULT, {
            {"agent_name", config.name},
            {"cmd", cmd},
            {"success", true},
            {"message", cmd == "check" ? "Local scripts ready" : "Local estop acknowledged"},
        });
        return;
    }
    const std::string script_name = params.value("script", cmd);
    const std::string scripts_dir = config.custom_params.value("scripts_dir", std::string("scripts"));
    std::filesystem::path script_path(script_name);
    if (script_path.is_relative()) {
        script_path = std::filesystem::path(nodes_root_) / scripts_dir / script_name;
    }
    if (!std::filesystem::exists(script_path)) {
        publishResult(msg::CMD_RESULT, {
            {"agent_name", config.name},
            {"cmd", cmd},
            {"success", false},
            {"message", "Script not found: " + script_name},
        });
        return;
    }

    std::vector<std::string> args;
    if (script_path.extension() == ".py") {
        args.push_back(python_bin_);
    } else if (script_path.extension() == ".sh") {
        args.push_back("bash");
    }
    args.push_back(script_path.string());
    const auto script_args = params.value("args", nlohmann::json::array());
    if (script_args.is_array()) {
        for (const auto& item : script_args) {
            if (item.is_string()) args.push_back(item.get<std::string>());
            else if (!item.is_null()) args.push_back(item.dump());
        }
    }

    try {
        if (!silent) {
            publishResult(msg::LOG_ENTRY, makeLogEntry("执行本地脚本命令: " + cmd, "info", "command"));
        }
        ProcessHandle process;
        process.start(args, nodes_root_, nodes_root_ + ":" + echo_python_root_);
        const int status = process.wait(config.commandTimeoutMs(cmd));
        const bool success = status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (status == -1) {
            process.terminate();
        }
        publishResult(msg::CMD_RESULT, {
            {"agent_name", config.name},
            {"cmd", cmd},
            {"success", success},
            {"message", success ? "Local script completed" : "Local script failed"},
        });
    } catch (const std::exception& e) {
        publishResult(msg::CMD_RESULT, {
            {"agent_name", config.name},
            {"cmd", cmd},
            {"success", false},
            {"message", e.what()},
        });
    }
}

void AgentManager::doShutdownAgent() {
    for (auto& [_, agent] : agents_) {
        if (agent) agent->disconnect();
    }
    agents_.clear();
    active_agent_.clear();
}

void AgentManager::doReleaseInactiveAgents() {
    for (auto it = agents_.begin(); it != agents_.end();) {
        if (!active_agent_.empty() && it->first == active_agent_) {
            ++it;
            continue;
        }
        if (it->second) {
            it->second->disconnect();
        }
        it = agents_.erase(it);
    }
}

AgentProxy* AgentManager::activeAgent() {
    return findAgent(active_agent_);
}

AgentProxy* AgentManager::findAgent(const std::string& agent_name) {
    auto it = agents_.find(agent_name);
    return it == agents_.end() ? nullptr : it->second.get();
}

AgentProxy* AgentManager::ensureAgentConnected(const std::string& agent_name, int connect_timeout_ms) {
    try {
        const auto config = AgentConfigLoader(agents_config_path_).loadAgent(agent_name);
        AgentProxy& agent = getOrCreateAgent(config);
        if (!agent.isConnected() && !agent.launchOrConnect(connect_timeout_ms)) {
            return nullptr;
        }
        return &agent;
    } catch (const std::exception& e) {
        common::Logger::instance().log(common::LogLevel::Error, "AgentManager",
            "ensure agent connected failed: " + std::string(e.what()),
            {{"agent_name", agent_name}});
        return nullptr;
    }
}

AgentProxy& AgentManager::getOrCreateAgent(const AgentConfig& config) {
    auto it = agents_.find(config.name);
    if (it == agents_.end()) {
        it = agents_.emplace(
            config.name,
            std::make_unique<AgentProxy>(
                config,
                agents_config_path_,
                nodes_root_,
                echo_python_root_,
                python_bin_,
                node_runtime_module_,
                [this](nlohmann::json payload) {
                    bus_.publish({
                        .source = msg::AGENT_MANAGER,
                        .target = msg::UI,
                        .type = msg::PROCESS_OUTPUT,
                        .payload = std::move(payload),
                    });
                })).first;
    }
    return *it->second;
}

void AgentManager::publishResult(const std::string& type, nlohmann::json payload) {
    if (type == msg::CMD_RESULT && !last_request_id_.empty()) {
        payload["request_id"] = last_request_id_;
    }
    // Route to both the original caller and UI for visibility.
    bus_.publish({
        .request_id = last_request_id_,
        .source = msg::AGENT_MANAGER,
        .target = last_source_,
        .type = type,
        .payload = payload,
    });
    if (last_source_ != msg::UI) {
        bus_.publish({
            .request_id = last_request_id_,
            .source = msg::AGENT_MANAGER,
            .target = msg::UI,
            .type = type,
            .payload = payload,
        });
    }
}

}  // namespace recordlab::host
