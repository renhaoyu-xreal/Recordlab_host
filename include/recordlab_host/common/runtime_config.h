#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace recordlab::host {

struct RuntimeConfig {
    std::string host_root;
    std::string agents_config_path;
    std::string nodes_root;
    std::string product_config_path;
    std::string echo_python_root;
    std::string data_root;
    std::string logs_root;
    std::string python_bin = "python3.10";
    std::string node_runtime_module = "recordlab_nodes.core.node_runtime";
    std::string data_registry_host = "127.0.0.1";
    int data_registry_port = 16600;
    std::string app_version = "v1.0.0";
    std::string update_info;
};

class RuntimeConfigLoader {
public:
    static RuntimeConfig load(const std::string& app_path,
                              const std::string& override_agents_config = {},
                              const std::string& runtime_config_path = {});

private:
    static nlohmann::json loadJsonIfExists(const std::string& path);
};

}  // namespace recordlab::host
