#include "recordlab_host/agents/agent_proxy.h"

#include "recordlab_host/common/logger.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace recordlab::host {

AgentProxy::AgentProxy(AgentConfig config, std::string agents_config_path,
                       std::string nodes_root, std::string echo_python_root)
    : config_(std::move(config)),
      agents_config_path_(std::move(agents_config_path)),
      nodes_root_(std::move(nodes_root)),
      echo_python_root_(std::move(echo_python_root)) {}

AgentProxy::~AgentProxy() {
    disconnect();
}

const std::string& AgentProxy::name() const {
    return config_.name;
}

const AgentConfig& AgentProxy::config() const {
    return config_;
}

bool AgentProxy::isConnected() const {
    return connected_;
}

bool AgentProxy::launchOrConnect() {
    if (connected_ && action_client_) return true;

    if (shouldLaunchLocalNode()) {
        startNodeProcess();
    } else {
        common::Logger::instance().log(
            common::LogLevel::Info,
            "AgentProxy",
            "connect remote/external node agent=" + config_.name +
                ", process_type=" + config_.process_type +
                ", host=" + config_.subnode_host);
    }

    connected_ = ensureClient();
    return connected_;
}

void AgentProxy::disconnect() {
    connected_ = false;
    action_client_.reset();
    if (node_process_) {
        node_process_->terminate();
        node_process_.reset();
    }
}

ActionResult AgentProxy::cmd(const std::string& cmd_name, const nlohmann::json& params, int timeout_ms) {
    if (!action_client_) {
        return {"", nlohmann::json{{"message", "当前没有可用 Agent client"}}, "DISCONNECTED", false};
    }
    return action_client_->sendCommand(cmd_name, params, timeout_ms);
}

bool AgentProxy::shouldLaunchLocalNode() const {
    return config_.process_type.empty() || config_.process_type == "python_node";
}

void AgentProxy::startNodeProcess() {
    if (node_process_ && node_process_->pid() > 0) return;

    node_process_ = std::make_unique<ProcessHandle>();
    const std::string pythonpath = nodes_root_ + ":" + echo_python_root_;
    const char* python_bin_env = std::getenv("RECORDLAB_PYTHON_BIN");
    const std::string python_bin = python_bin_env && *python_bin_env ? python_bin_env : "python3.10";
    common::Logger::instance().log(
        common::LogLevel::Info,
        "AgentProxy",
        "launch node_runtime agent=" + config_.name +
            ", python=" + python_bin +
            ", cwd=" + nodes_root_ +
            ", pythonpath=" + pythonpath);
    node_process_->start(
        {python_bin, "-m", "recordlab_nodes.core.node_runtime",
         "--config", agents_config_path_, "--agent", config_.name},
        nodes_root_, pythonpath);
}

bool AgentProxy::ensureClient() {
    if (!action_client_) {
        action_client_ = std::make_unique<EchoActionClient>(
            config_.subnode_host, config_.goal_port, config_.feedback_port, 3000);
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

}  // namespace recordlab::host
