#pragma once

#include "recordlab_host/agents/agent_config_loader.h"
#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/common/process_handle.h"
#include "recordlab_host/communication/echo_action_client.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace recordlab::host {

/// AgentManager (PLAN.md T2) — runs in its own worker thread.
/// Owns the Python node process and EchoActionClient.
/// Receives commands from the bus, executes them (potentially blocking),
/// and publishes results back to the bus.
class AgentManager {
public:
    AgentManager(HostMessageBus& bus, std::string agents_config_path,
                 std::string nodes_root, std::string echo_python_root);
    ~AgentManager();

    AgentManager(const AgentManager&) = delete;
    AgentManager& operator=(const AgentManager&) = delete;

    void start();   ///< Start the worker thread.
    void stop();    ///< Stop the worker thread and release resources.

    /// Thread-safe config queries (called from UI thread).
    std::vector<std::string> primaryAgents() const;
    AgentConfig loadAgentConfig(const std::string& agent_name) const;

private:
    void workerLoop();
    void handleMessage(const HostMessage& msg);
    void doActivateAgent(const std::string& agent_name);
    void doCmdRequest(const std::string& cmd, const nlohmann::json& params);
    void doShutdownAgent();

    void startNodeProcess(const AgentConfig& config);
    bool ensureClient(const AgentConfig& config);
    void publishToUI(const std::string& type, nlohmann::json payload);

    HostMessageBus& bus_;
    std::string agents_config_path_;
    std::string nodes_root_;
    std::string echo_python_root_;

    std::unique_ptr<ProcessHandle> node_process_;
    std::unique_ptr<EchoActionClient> action_client_;
    std::string active_agent_;
    std::string node_process_agent_;

    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace recordlab::host
