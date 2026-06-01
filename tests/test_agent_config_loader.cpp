#include "recordlab_host/agents/agent_config_loader.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

std::filesystem::path hostRoot() {
    const auto cwd = std::filesystem::current_path();
    return cwd.filename() == "build" ? cwd.parent_path() : cwd;
}

std::string defaultAgentsConfig() {
    if (const char* env = std::getenv("RECORDLAB_AGENTS_CONFIG")) {
        return env;
    }
    return (hostRoot() / "third_party" / "Recordlab_nodes" / "config" / "agents_config.json").string();
}

}  // namespace

int main() {
    recordlab::host::AgentConfigLoader loader(defaultAgentsConfig());
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
