#pragma once

#include "recordlab_host/agents/agent_config_loader.h"
#include "recordlab_host/common/process_handle.h"
#include "recordlab_host/communication/echo_action_client.h"

#include <memory>
#include <functional>
#include <string>

namespace recordlab::host {

/// Host-side proxy for one configured node.
///
/// AgentProxy owns the node shell: local process lifecycle and command channel.
/// It does not own device objects, topic subscriptions, workflow state, or UI.
class AgentProxy {
public:
    using ProcessOutputCallback = std::function<void(nlohmann::json)>;

    AgentProxy(AgentConfig config, std::string agents_config_path,
               std::string nodes_root, std::string echo_python_root,
               std::string python_bin, std::string node_runtime_module,
               ProcessOutputCallback process_output_callback = {});
    ~AgentProxy();

    AgentProxy(const AgentProxy&) = delete;
    AgentProxy& operator=(const AgentProxy&) = delete;

    const std::string& name() const;
    const AgentConfig& config() const;
    bool isConnected() const;

    bool launchOrConnect(int connect_timeout_ms = 0);
    void disconnect();
    ActionResult cmd(const std::string& cmd, const nlohmann::json& params, int timeout_ms = 5000);

private:
    bool shouldLaunchLocalNode() const;
    void startNodeProcess();
    bool ensureClient(int connect_timeout_ms = 0);

    AgentConfig config_;
    std::string agents_config_path_;
    std::string nodes_root_;
    std::string echo_python_root_;
    std::string python_bin_;
    std::string node_runtime_module_;
    ProcessOutputCallback process_output_callback_;

    std::unique_ptr<ProcessHandle> node_process_;
    std::unique_ptr<EchoActionClient> action_client_;
    bool connected_ = false;
};

}  // namespace recordlab::host
