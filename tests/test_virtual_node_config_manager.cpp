#include "recordlab_host/common/virtual_node_config_manager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

nlohmann::json loadJson(const fs::path& path) {
    std::ifstream input(path);
    require(static_cast<bool>(input), "failed to open json file");
    nlohmann::json doc;
    input >> doc;
    return doc;
}

}  // namespace

int main() {
    const fs::path temp_root = fs::temp_directory_path()
        / ("recordlab_virtual_node_config_" + std::to_string(::getpid()));
    fs::create_directories(temp_root);

    const fs::path original_config = temp_root / "agents_config.json";
    std::ofstream output(original_config);
    output << R"({
      "agents": {
        "imu_simulation": {
          "name": "imu_simulation",
          "node_class": "recordlab_nodes.nodes.imu_sim.imu_sim_node.ImuSimNode",
          "process_type": "python_node",
          "goal_port": 5690,
          "feedback_port": 5691,
          "data_port": 16510,
          "root_path": "data",
          "init_device_params": {
            "read_path": "config/demo.txt"
          }
        },
        "glasses_nviz_node": {
          "name": "glasses_nviz_node",
          "node_class": "recordlab_nodes.nodes.nviz.nviz_node.NVizNode",
          "process_type": "python_node",
          "goal_port": 5692,
          "feedback_port": 5693,
          "data_port": 16511,
          "root_path": "data"
        },
        "UR_node": {
          "name": "UR_node",
          "subnode_host": "192.168.10.213",
          "action_name": "UR_node_actions",
          "goal_port": 5557,
          "feedback_port": 5558,
          "custom_params": {
            "remote_user": "xreal",
            "remote_password": "Xreal0125"
          }
        }
      },
      "primary_agents": ["imu_simulation"]
    })";
    output.close();

    recordlab::host::VirtualNodeConfigManager manager(
        original_config.string(), temp_root.string());

    require(fs::exists(manager.effectiveConfigPath()), "effective config should be created");
    auto disabled_doc = loadJson(manager.effectiveConfigPath());
    require(disabled_doc["agents"]["UR_node"].value("subnode_host", std::string{}) == "192.168.10.213",
            "disabled config should preserve real UR host");
    require(!disabled_doc["agents"]["UR_node"].contains("node_class"),
            "disabled config should not inject virtual node class");
    require(disabled_doc["agents"]["imu_simulation"].value("goal_port", 0) == 5690,
            "other agents should remain unchanged when disabled");
    require(disabled_doc["agents"]["imu_simulation"].value("root_path", std::string{}) ==
                (temp_root / "data").string(),
            "disabled config should absolutize relative root_path");
    require(disabled_doc["agents"]["imu_simulation"]["init_device_params"].value("read_path", std::string{}) ==
                (temp_root / "config" / "demo.txt").string(),
            "disabled config should absolutize relative init_device_params.read_path");
    require(disabled_doc["agents"]["glasses_nviz_node"].value("root_path", std::string{}) ==
                (temp_root / "data").string(),
            "disabled config should preserve other agent data root semantics");

    auto config = manager.virtualUrConfig();
    config.enabled = true;
    config.trajectory_duration_s = 21;
    config.trajectory_file_size_mib = 40;
    config.trajectory_return_rate_mib_per_s = 10;
    manager.setVirtualUrConfig(config);

    auto enabled_doc = loadJson(manager.effectiveConfigPath());
    const auto& virtual_ur = enabled_doc["agents"]["UR_node"];
    require(virtual_ur.value("process_type", std::string{}) == "python_node",
            "enabled config should route UR_node to local python node");
    require(virtual_ur.value("node_class", std::string{}) ==
                "recordlab_nodes.nodes.virtual.ur_virtual_node.VirtualUrNode",
            "enabled config should inject virtual UR node class");
    require(virtual_ur.value("subnode_host", std::string{}) == "127.0.0.1",
            "enabled config should route to localhost");
    require(virtual_ur.value("data_port", 0) == 16570,
            "enabled config should assign virtual UR data port");
    require(virtual_ur.value("root_path", std::string{}) ==
                (temp_root / "data" / "virtual_ur").string(),
            "enabled config should point virtual UR to host-local data path");
    require(virtual_ur["custom_params"].value("virtual_node", false),
            "enabled config should mark virtual UR");
    require(virtual_ur["custom_params"].value("copy_mode", std::string{}) == "local_fs",
            "enabled config should use local_fs copy mode");
    require(virtual_ur["custom_params"].value("trajectory_duration_s", 0) == 21,
            "enabled config should persist duration");
    require(virtual_ur["custom_params"].value("trajectory_file_size_mib", 0) == 40,
            "enabled config should persist file size");
    require(virtual_ur["custom_params"].value("trajectory_return_rate_mib_per_s", 0) == 10,
            "enabled config should persist return rate");
    require(enabled_doc["agents"]["imu_simulation"].value("goal_port", 0) == 5690,
            "other agents should remain unchanged when enabled");
    require(enabled_doc["agents"]["glasses_nviz_node"].value("root_path", std::string{}) ==
                (temp_root / "data").string(),
            "enabled config should keep relative root_path agents on original data root");

    fs::remove_all(temp_root);
    std::cout << "virtual node config manager ok\n";
    return 0;
}
