#include "recordlab_host/agents/agent_manager.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <sstream>

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
    } else if (msg.type == msg::CMD_REQUEST) {
        const auto agent_name = msg.payload.value("agent_name", active_agent_);
        const auto cmd = msg.payload.value("cmd", std::string{});
        const auto params = msg.payload.value("params", nlohmann::json::object());
        const auto silent = msg.payload.value("silent", false);
        doCmdRequest(agent_name, cmd, params, silent);
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
        publishResult(msg::LOG_ENTRY, {{"message", "启动 Agent: " + agent_name}});
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
        AgentProxy& agent = getOrCreateAgent(config);
        if (agent.launchOrConnect()) {
            publishResult(msg::LOG_ENTRY, {{"message", "Agent " + agent_name + " 连接成功，等待 Watchdog 初始化"}});
            publishResult(msg::AGENT_ACTIVATED, {
                {"agent_name", agent_name}, {"success", true},
                {"message", "Agent activated"},
                {"subnode_host", config.subnode_host},
                {"topics", [&]() {
                    nlohmann::json arr = nlohmann::json::array();
                    for (const auto& t : config.topics)
                        arr.push_back({{"name", t.name}, {"port", config.data_port}, {"encoding", t.encoding}});
                    return arr;
                }()},
                {"init_device_params", config.init_device_params},
            });
        } else {
            publishResult(msg::LOG_ENTRY, {{"message", "Agent " + agent_name + " 连接失败"}});
            publishResult(msg::AGENT_ACTIVATED, {
                {"agent_name", agent_name}, {"success", false}, {"message", "Connection failed"},
            });
        }
    } catch (const std::exception& e) {
        publishResult(msg::LOG_ENTRY, {{"message", std::string("Agent 启动失败: ") + e.what()}});
        publishResult(msg::AGENT_ACTIVATED, {
            {"agent_name", agent_name}, {"success", false}, {"message", e.what()},
        });
    }
}

void AgentManager::doCmdRequest(const std::string& agent_name, const std::string& cmd,
                                const nlohmann::json& params, bool silent) {
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
        publishResult(msg::CMD_RESULT, {
            {"agent_name", target_agent},
            {"cmd", cmd},
            {"success", false},
            {"message", "请求 Agent 与当前 active Agent 不一致: " + target_agent + " != " + active_agent_},
        });
        return;
    }
    AgentProxy* agent = findAgent(target_agent);
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
            publishResult(msg::LOG_ENTRY, {{"message", "发送命令: " + cmd + " " + params.dump()}});
        }
        common::Logger::instance().log(common::LogLevel::Info, "AgentManager", "sending command", {
            {"request_id", last_request_id_},
            {"agent_name", target_agent},
            {"cmd", cmd},
        });
        const auto result = agent->cmd(cmd, params, agent->config().commandTimeoutMs(cmd));
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
            publishResult(msg::LOG_ENTRY, {{"message", cmd + ": " + message}});
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
            publishResult(msg::LOG_ENTRY, {{"message", cmd + " 失败: " + std::string(e.what())}});
        }
    }
}

void AgentManager::doShutdownAgent() {
    for (auto& [_, agent] : agents_) {
        if (agent) agent->disconnect();
    }
    agents_.clear();
    active_agent_.clear();
}

AgentProxy* AgentManager::activeAgent() {
    return findAgent(active_agent_);
}

AgentProxy* AgentManager::findAgent(const std::string& agent_name) {
    auto it = agents_.find(agent_name);
    return it == agents_.end() ? nullptr : it->second.get();
}

AgentProxy& AgentManager::getOrCreateAgent(const AgentConfig& config) {
    if (!active_agent_.empty() && active_agent_ != config.name) {
        if (AgentProxy* current = activeAgent()) current->disconnect();
    }
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
    active_agent_ = config.name;
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
