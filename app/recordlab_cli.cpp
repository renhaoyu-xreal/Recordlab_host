#include "recordlab_host/agents/agent_config_loader.h"

#include <filesystem>
#include <iostream>

namespace {

std::string defaultAgentsConfig(char** argv) {
    namespace fs = std::filesystem;
    fs::path bin_path = fs::absolute(argv[0]).parent_path();
    fs::path host_root = bin_path.filename() == "build" ? bin_path.parent_path() : bin_path;
    return (host_root / "third_party" / "Recordlab_nodes" / "config" / "agents_config.json").string();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string config = argc > 1 ? argv[1] : defaultAgentsConfig(argv);
    recordlab::host::AgentConfigLoader loader(config);
    for (const auto& name : loader.loadPrimaryAgents()) {
        auto agent = loader.loadAgent(name);
        std::cout << agent.name << " -> " << agent.node_class << "\n";
    }
    return 0;
}
