#include "recordlab_host/agents/agent_manager.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace recordlab::host {
namespace {

std::string describeTopics(const std::vector<TopicConfig>& topics) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << topics[i].name << "@" << topics[i].port << "/" << topics[i].encoding;
    }
    return oss.str();
}

}  // namespace

AgentManager::AgentManager(HostMessageBus& bus, std::string agents_config_path,
                           std::string nodes_root, std::string echo_python_root)
    : bus_(bus),
      agents_config_path_(std::move(agents_config_path)),
      nodes_root_(std::move(nodes_root)),
      echo_python_root_(std::move(echo_python_root)) {
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
    if (msg.type == msg::SHUTDOWN_AGENT) {
        return;  // workerLoop will exit via running_ flag
    }
    if (msg.type == msg::ACTIVATE_AGENT) {
        const auto agent_name = msg.payload.value("agent_name", std::string{});
        doActivateAgent(agent_name);
    } else if (msg.type == msg::CMD_REQUEST) {
        const auto cmd = msg.payload.value("cmd", std::string{});
        const auto params = msg.payload.value("params", nlohmann::json::object());
        doCmdRequest(cmd, params);
    }
}

void AgentManager::doActivateAgent(const std::string& agent_name) {
    try {
        active_agent_ = agent_name;
        const auto config = AgentConfigLoader(agents_config_path_).loadAgent(agent_name);
        publishToUI(msg::LOG_ENTRY, {{"message", "启动 Agent: " + agent_name}});
        common::Logger::instance().log(
            common::LogLevel::Info,
            "AgentManager",
            "activate agent=" + config.name +
                ", node_class=" + config.node_class +
                ", action=" + config.action_name +
                ", goal_port=" + std::to_string(config.goal_port) +
                ", feedback_port=" + std::to_string(config.feedback_port) +
                ", topics=[" + describeTopics(config.topics) + "]");
        startNodeProcess(config);
        if (ensureClient(config)) {
            publishToUI(msg::WATCHDOG_STATE, {{"state", "HEALTHY"}});
            publishToUI(msg::LOG_ENTRY, {{"message", "Watchdog: " + agent_name + " check 成功"}});
            if (!config.init_device_params.empty()) {
                const auto result = action_client_->sendCommand("init_device", config.init_device_params, 5000);
                publishToUI(msg::CMD_RESULT, {
                    {"cmd", "init_device"}, {"success", result.success},
                    {"message", result.result.value("message", result.result.dump())},
                });
                publishToUI(msg::LOG_ENTRY, {
                    {"message", "init_device: " + result.result.value("message", result.result.dump())},
                });
            }
            publishToUI(msg::AGENT_ACTIVATED, {
                {"agent_name", agent_name}, {"success", true}, {"message", "Agent activated"},
            });
        } else {
            publishToUI(msg::WATCHDOG_STATE, {{"state", "DISCONNECTED"}});
            publishToUI(msg::LOG_ENTRY, {{"message", "Watchdog: " + agent_name + " 连接失败"}});
            publishToUI(msg::AGENT_ACTIVATED, {
                {"agent_name", agent_name}, {"success", false}, {"message", "Connection failed"},
            });
        }
    } catch (const std::exception& e) {
        publishToUI(msg::WATCHDOG_STATE, {{"state", "DISCONNECTED"}});
        publishToUI(msg::LOG_ENTRY, {{"message", std::string("Agent 启动失败: ") + e.what()}});
        publishToUI(msg::AGENT_ACTIVATED, {
            {"agent_name", agent_name}, {"success", false}, {"message", e.what()},
        });
    }
}

void AgentManager::doCmdRequest(const std::string& cmd, const nlohmann::json& params) {
    if (!action_client_) {
        publishToUI(msg::CMD_RESULT, {{"cmd", cmd}, {"success", false}, {"message", "当前没有可用 Agent client"}});
        return;
    }
    try {
        publishToUI(msg::LOG_ENTRY, {{"message", "发送命令: " + cmd + " " + params.dump()}});
        const auto result = action_client_->sendCommand(cmd, params, 5000);
        const auto message = result.result.value("message", result.result.dump());
        publishToUI(msg::CMD_RESULT, {{"cmd", cmd}, {"success", result.success}, {"message", message}});
        publishToUI(msg::LOG_ENTRY, {{"message", cmd + ": " + message}});
    } catch (const std::exception& e) {
        publishToUI(msg::CMD_RESULT, {{"cmd", cmd}, {"success", false}, {"message", e.what()}});
        publishToUI(msg::LOG_ENTRY, {{"message", cmd + " 失败: " + std::string(e.what())}});
    }
}

void AgentManager::doShutdownAgent() {
    action_client_.reset();
    if (node_process_) {
        node_process_->terminate();
        node_process_.reset();
    }
    node_process_agent_.clear();
    active_agent_.clear();
}

void AgentManager::startNodeProcess(const AgentConfig& config) {
    if (node_process_ && node_process_->pid() > 0 && node_process_agent_ == config.name) return;
    if (node_process_) {
        action_client_.reset();
        node_process_->terminate();
        node_process_.reset();
        node_process_agent_.clear();
    }
    node_process_ = std::make_unique<ProcessHandle>();
    const std::string pythonpath = nodes_root_ + ":" + echo_python_root_;
    const char* python_bin_env = std::getenv("RECORDLAB_PYTHON_BIN");
    const std::string python_bin = python_bin_env && *python_bin_env ? python_bin_env : "python3.10";
    common::Logger::instance().log(
        common::LogLevel::Info,
        "AgentManager",
        "launch node_runtime agent=" + config.name +
            ", python=" + python_bin +
            ", cwd=" + nodes_root_ +
            ", pythonpath=" + pythonpath);
    node_process_->start(
        {python_bin, "-m", "recordlab_nodes.core.node_runtime",
         "--config", agents_config_path_, "--agent", config.name},
        nodes_root_, pythonpath);
    node_process_agent_ = config.name;
    publishToUI(msg::LOG_ENTRY, {{"message", "node_runtime pid=" + std::to_string(node_process_->pid())}});
}

bool AgentManager::ensureClient(const AgentConfig& config) {
    if (!action_client_) {
        action_client_ = std::make_unique<EchoActionClient>(
            config.subnode_host, config.goal_port, config.feedback_port, 3000);
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (action_client_->waitForServer(1000)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

void AgentManager::publishToUI(const std::string& type, nlohmann::json payload) {
    bus_.publish({.source = msg::AGENT_MANAGER, .target = msg::UI, .type = type, .payload = std::move(payload)});
}

}  // namespace recordlab::host
