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
            assert(topic.ui_max_hz == 30.0);
            assert(topic.qos.value("history", std::string{}) == "latest");
            assert(topic.qos.value("depth", 0) == 1);
        }
        if (topic.name == "imu_data") {
            assert(topic.parse_mode == "type_vector6_fast");
        }
    }
    assert(has_camera);
    auto mcu = loader.loadAgent("mcu_node");
    assert(mcu.name == "mcu_node");
    assert(mcu.node_class.find("McuMainNode") != std::string::npos);
    assert(mcu.goal_port == 5696);
    assert(mcu.data_port == 16536);
    assert(!mcu.default_scripts.empty());
    assert(!mcu.exposed_commands.empty());
    assert(mcu.commandTimeoutMs("start_device") == 90000);
    assert(mcu.sensor_layout.contains("imu_data"));
    assert(mcu.error_messages.contains("INIT_DEVICE_FAILED"));
    assert(mcu.ui_bindings.contains("record_timer"));
    assert(mcu.init_device_params.value("allow_ssh_reboot", true) == false);
    assert(mcu.custom_params.value("persist_ssh_artifacts", true) == false);

    auto ur = loader.loadAgent("UR_node");
    assert(ur.name == "UR_node");
    assert(ur.process_type == "external_action");
    assert(ur.node_class.empty());
    assert(ur.goal_port == 5557);
    assert(ur.feedback_port == 5558);
    assert(ur.data_port == 0);

    auto localhost = loader.loadAgent("localhost");
    assert(localhost.name == "localhost");
    assert(localhost.process_type == "local_scripts");
    assert(localhost.custom_params.value("scripts_dir", std::string{}) == "node_scripts/localhost");

    const auto tmp = hostRoot() / "build" / "test_agent_config_loader_tmp.json";
    {
        std::ofstream out(tmp);
        out << R"({"agents":{"bad":{"node_class":"x.y.Node","goal_port":1,"feedback_port":2,"topics":"empty"}},"shared":{"topic_sets":{"empty":[]}}})";
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
        out << R"({"agents":{"bad":{"node_class":"x.y.Node","goal_port":1,"feedback_port":2,"data_port":3,"topics":"bad_topics"}},"shared":{"topic_sets":{"bad_topics":[{"name":"imu_data","port":4,"encoding":"json"}]}}})";
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
