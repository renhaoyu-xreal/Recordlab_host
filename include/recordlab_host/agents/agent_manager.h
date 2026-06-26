#pragma once

#include "recordlab_host/agents/agent_config_loader.h"
#include "recordlab_host/agents/agent_proxy.h"
#include "recordlab_host/bus/host_message_bus.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace recordlab::host {

/// AgentManager (PLAN.md T2) — runs in its own worker thread.
/// Owns the Python node process and EchoActionClient.
/// Receives commands from the bus, executes them (potentially blocking),
/// and publishes results back to the bus.
class AgentManager {
public:
    AgentManager(HostMessageBus& bus, std::string agents_config_path,
                 std::string nodes_root, std::string echo_python_root,
                 std::string python_bin, std::string node_runtime_module);
    ~AgentManager();

    AgentManager(const AgentManager&) = delete;
    AgentManager& operator=(const AgentManager&) = delete;

    void start();   ///< Start the worker thread.
    void stop();    ///< Stop the worker thread and release resources.

    /// Thread-safe config queries (called from UI thread).
    std::vector<std::string> primaryAgents() const;
    AgentConfig loadAgentConfig(const std::string& agent_name) const;

private:
    enum class WorkerLane {
        Normal,
        Health,
        Priority,
    };

    struct MessageContext {
        std::string source;
        std::string request_id;
    };

    void workerLoop(const std::string& target, WorkerLane lane);
    void handleMessage(const HostMessage& msg, WorkerLane lane);
    void doActivateAgent(const std::string& agent_name, const MessageContext& context);
    void doCmdRequest(const std::string& agent_name, const std::string& cmd,
                      const nlohmann::json& params, bool silent,
                      const MessageContext& context, int timeout_ms = 0);
    void doHealthCmdRequest(const std::string& agent_name, const std::string& cmd,
                            const nlohmann::json& params, bool silent,
                            const MessageContext& context, int timeout_ms = 0);
    void doPriorityCmdRequest(const std::string& agent_name, const std::string& cmd,
                              const nlohmann::json& params, bool silent,
                              const MessageContext& context, int timeout_ms = 0);
    void doLocalScriptCommand(const AgentConfig& config, const std::string& cmd,
                              const nlohmann::json& params, bool silent,
                              const MessageContext& context);
    void doShutdownAgent();
    void doReleaseInactiveAgents();

    AgentProxy* activeAgent();
    AgentProxy* findAgent(const std::string& agent_name);
    AgentProxy* ensureAgentConnected(const std::string& agent_name, int connect_timeout_ms = 0);
    EchoActionClient* ensureHealthClient(const AgentConfig& config, int connect_timeout_ms);
    EchoActionClient* ensurePriorityClient(const AgentConfig& config, int connect_timeout_ms);
    void dropHealthClient(const std::string& agent_name);
    void dropPriorityClient(const std::string& agent_name);
    AgentProxy& getOrCreateAgent(const AgentConfig& config);
    void publishResult(const MessageContext& context, const std::string& type, nlohmann::json payload);

    HostMessageBus& bus_;
    std::string agents_config_path_;
    std::string nodes_root_;
    std::string echo_python_root_;
    std::string python_bin_;
    std::string node_runtime_module_;

    std::unordered_map<std::string, std::unique_ptr<AgentProxy>> agents_;
    std::unordered_map<std::string, std::unique_ptr<EchoActionClient>> health_clients_;
    std::unordered_map<std::string, std::unique_ptr<EchoActionClient>> priority_clients_;
    std::string active_agent_;

    std::thread worker_;
    std::thread health_worker_;
    std::thread priority_worker_;
    std::atomic<bool> running_{false};
};

}  // namespace recordlab::host
