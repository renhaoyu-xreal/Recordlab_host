#include "recordlab_host/agents/agent_config_loader.h"

#include <cassert>
#include <iostream>

int main() {
    recordlab::host::AgentConfigLoader loader("/home/hyren/Recordlab_nodes/config/agents_config.json");
    auto primary = loader.loadPrimaryAgents();
    assert(!primary.empty());
    auto agent = loader.loadAgent("imu_simulation");
    assert(agent.name == "imu_simulation");
    assert(agent.node_class.find("ImuSimNode") != std::string::npos);
    assert(agent.goal_port == 5690);
    assert(!agent.topics.empty());
    std::cout << "agent config loader ok\n";
    return 0;
}
