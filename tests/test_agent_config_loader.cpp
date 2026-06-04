#include "recordlab_host/agents/agent_config_loader.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    assert(agent.data_port == 16510);
    assert(!agent.topics.empty());
    auto bsp = loader.loadAgent("glasses_bsp_node");
    assert(bsp.name == "glasses_bsp_node");
    assert(bsp.node_class.find("BspMainNode") != std::string::npos);
    assert(!bsp.default_scripts.empty());
    bool has_camera = false;
    for (const auto& topic : bsp.topics) {
        if (topic.name == "camera_data" && topic.encoding == "json_binary") {
            has_camera = true;
        }
    }
    assert(has_camera);

    const auto tmp = hostRoot() / "build" / "test_agent_config_loader_tmp.json";
    {
        std::ofstream out(tmp);
        out << R"({"agents":{"bad":{"node_class":"x.y.Node","goal_port":1,"feedback_port":2,"topics":[]}}})";
    }
    bool missing_data_port_failed = false;
    try {
        recordlab::host::AgentConfigLoader(tmp.string()).loadAgent("bad");
    } catch (const std::exception&) {
        missing_data_port_failed = true;
    }
    assert(missing_data_port_failed);

    {
        std::ofstream out(tmp);
        out << R"({"agents":{"bad":{"node_class":"x.y.Node","goal_port":1,"feedback_port":2,"data_port":3,"topics":[{"name":"imu_data","port":4,"encoding":"json"}]}}})";
    }
    bool topic_port_failed = false;
    try {
        recordlab::host::AgentConfigLoader(tmp.string()).loadAgent("bad");
    } catch (const std::exception&) {
        topic_port_failed = true;
    }
    assert(topic_port_failed);

    std::cout << "agent config loader ok\n";
    return 0;
}
