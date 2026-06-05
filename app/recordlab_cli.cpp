#include "recordlab_host/agents/agent_config_loader.h"
#include "recordlab_host/common/runtime_config.h"

#include <iostream>

int main(int argc, char** argv) {
    const auto runtime = recordlab::host::RuntimeConfigLoader::load(
        argv[0], argc > 1 ? argv[1] : std::string{});
    const std::string config = runtime.agents_config_path;
    recordlab::host::AgentConfigLoader loader(config);
    for (const auto& name : loader.loadPrimaryAgents()) {
        auto agent = loader.loadAgent(name);
        std::cout << agent.name << " -> " << agent.node_class << "\n";
    }
    return 0;
}
