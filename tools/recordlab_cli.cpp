#include "recordlab_host/agents/agent_config_loader.h"

#include <iostream>

int main(int argc, char** argv) {
    const std::string config = argc > 1 ? argv[1] : "/home/hyren/Recordlab_nodes/config/agents_config.json";
    recordlab::host::AgentConfigLoader loader(config);
    for (const auto& name : loader.loadPrimaryAgents()) {
        auto agent = loader.loadAgent(name);
        std::cout << agent.name << " -> " << agent.node_class << "\n";
    }
    return 0;
}
