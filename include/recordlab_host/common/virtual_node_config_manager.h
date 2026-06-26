#pragma once

#include <string>

namespace recordlab::host {

struct VirtualUrNodeConfig {
    bool enabled = false;
    int trajectory_duration_s = 20;
    int trajectory_file_size_mib = 32;
    int trajectory_return_rate_mib_per_s = 8;
};

class VirtualNodeConfigManager {
public:
    VirtualNodeConfigManager(std::string original_agents_config_path, std::string nodes_root);

    const std::string& originalConfigPath() const;
    const std::string& effectiveConfigPath() const;
    const VirtualUrNodeConfig& virtualUrConfig() const;

    void setVirtualUrConfig(VirtualUrNodeConfig config);

private:
    void writeEffectiveConfig() const;

    std::string original_agents_config_path_;
    std::string nodes_root_;
    std::string effective_agents_config_path_;
    VirtualUrNodeConfig virtual_ur_config_;
};

}  // namespace recordlab::host
