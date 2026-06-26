#include "recordlab_host/common/virtual_node_config_manager.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace recordlab::host {
namespace fs = std::filesystem;

namespace {

nlohmann::json loadJson(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open agents config: " + path);
    }
    nlohmann::json doc;
    input >> doc;
    return doc;
}

std::string buildEffectiveConfigPath() {
    const fs::path dir = fs::temp_directory_path()
        / ("recordlab_virtual_nodes_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    return (dir / "agents_config.effective.json").string();
}

fs::path originalConfigBasePath(const std::string& original_config_path) {
    const fs::path config_file = fs::path(original_config_path).lexically_normal();
    const fs::path config_dir = config_file.parent_path();
    if (config_dir.filename() == "config") {
        return config_dir.parent_path();
    }
    return config_dir;
}

std::string resolveConfigPathString(const std::string& value, const fs::path& base) {
    if (value.empty()) {
        return value;
    }
    const fs::path path = fs::path(value).lexically_normal();
    if (path.is_absolute()) {
        return path.string();
    }
    return (base / path).lexically_normal().string();
}

void absolutizeAgentPaths(nlohmann::json& agent, const fs::path& base) {
    if (!agent.is_object()) {
        return;
    }

    auto root_path_it = agent.find("root_path");
    if (root_path_it != agent.end() && root_path_it->is_string()) {
        *root_path_it = resolveConfigPathString(root_path_it->get<std::string>(), base);
    }

    auto init_device_params_it = agent.find("init_device_params");
    if (init_device_params_it == agent.end() || !init_device_params_it->is_object()) {
        return;
    }

    auto read_path_it = init_device_params_it->find("read_path");
    if (read_path_it != init_device_params_it->end() && read_path_it->is_string()) {
        *read_path_it = resolveConfigPathString(read_path_it->get<std::string>(), base);
    }
}

}  // namespace

VirtualNodeConfigManager::VirtualNodeConfigManager(std::string original_agents_config_path,
                                                   std::string nodes_root)
    : original_agents_config_path_(std::move(original_agents_config_path)),
      nodes_root_(std::move(nodes_root)),
      effective_agents_config_path_(buildEffectiveConfigPath()) {
    writeEffectiveConfig();
}

const std::string& VirtualNodeConfigManager::originalConfigPath() const {
    return original_agents_config_path_;
}

const std::string& VirtualNodeConfigManager::effectiveConfigPath() const {
    return effective_agents_config_path_;
}

const VirtualUrNodeConfig& VirtualNodeConfigManager::virtualUrConfig() const {
    return virtual_ur_config_;
}

void VirtualNodeConfigManager::setVirtualUrConfig(VirtualUrNodeConfig config) {
    virtual_ur_config_ = config;
    writeEffectiveConfig();
}

void VirtualNodeConfigManager::writeEffectiveConfig() const {
    auto doc = loadJson(original_agents_config_path_);
    if (!doc.is_object()) {
        throw std::runtime_error("agents config must be a JSON object");
    }
    auto& agents = doc["agents"];
    if (!agents.is_object()) {
        throw std::runtime_error("agents config missing object field: agents");
    }

    const fs::path config_base = originalConfigBasePath(original_agents_config_path_);
    for (auto& agent_entry : agents.items()) {
        absolutizeAgentPaths(agent_entry.value(), config_base);
    }

    if (virtual_ur_config_.enabled) {
        agents["UR_node"] = {
            {"name", "UR_node"},
            {"process_type", "python_node"},
            {"node_class", "recordlab_nodes.nodes.virtual.ur_virtual_node.VirtualUrNode"},
            {"subnode_host", "127.0.0.1"},
            {"action_name", "UR_node_actions"},
            {"goal_port", 5557},
            {"feedback_port", 5558},
            {"data_port", 16570},
            {"root_path", (fs::path(nodes_root_) / "data" / "virtual_ur").string()},
            {"custom_params", {
                {"virtual_node", true},
                {"copy_mode", "local_fs"},
                {"trajectory_duration_s", virtual_ur_config_.trajectory_duration_s},
                {"trajectory_file_size_mib", virtual_ur_config_.trajectory_file_size_mib},
                {"trajectory_return_rate_mib_per_s", virtual_ur_config_.trajectory_return_rate_mib_per_s},
            }},
        };
    }

    std::ofstream output(effective_agents_config_path_, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to write effective agents config: " + effective_agents_config_path_);
    }
    output << doc.dump(2);
}

}  // namespace recordlab::host
