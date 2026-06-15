#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace recordlab::host {

struct TopicConfig {
    std::string name;
    std::string encoding = "json";
    std::string parse_mode = "json";
    double ui_max_hz = 30.0;
    nlohmann::json qos = nlohmann::json::object();
    nlohmann::json metadata = nlohmann::json::object();
};

struct CommandConfig {
    std::string name;
    int timeout_ms = 0;
    nlohmann::json params_schema = nlohmann::json::object();
};

struct CommandsConfig {
    int default_timeout_ms = 5000;
    std::vector<CommandConfig> items;
};

struct AgentConfig {
    std::string name;
    std::string node_class;
    std::string process_type;
    std::string subnode_host;
    std::string action_name;
    int goal_port = 0;
    int feedback_port = 0;
    int data_port = 0;
    std::string root_path;
    nlohmann::json init_device_params = nlohmann::json::object();
    bool watchdog_start_device = true;
    std::vector<TopicConfig> topics;
    std::vector<std::string> default_scripts;
    std::vector<std::string> exposed_commands;
    CommandsConfig commands;
    nlohmann::json sensor_layout = nlohmann::json::object();
    nlohmann::json error_messages = nlohmann::json::object();
    nlohmann::json ui_bindings = nlohmann::json::object();
    nlohmann::json custom_params = nlohmann::json::object();

    int commandTimeoutMs(const std::string& cmd) const;
};

class AgentConfigLoader {
public:
    explicit AgentConfigLoader(std::string path);

    AgentConfig loadAgent(const std::string& agent_name) const;
    std::vector<std::string> loadPrimaryAgents() const;

private:
    std::string path_;
    nlohmann::json loadJson() const;
};

}  // namespace recordlab::host
