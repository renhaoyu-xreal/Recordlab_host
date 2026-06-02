#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace recordlab::host {

struct TopicConfig {
    std::string name;
    int port = 0;
    std::string encoding = "json";
    std::string parse_mode = "json";
    double ui_max_hz = 30.0;
    nlohmann::json qos = nlohmann::json::object();
};

struct AgentConfig {
    std::string name;
    std::string node_class;
    std::string process_type;
    std::string subnode_host;
    std::string action_name;
    int goal_port = 0;
    int feedback_port = 0;
    std::string root_path;
    nlohmann::json init_device_params = nlohmann::json::object();
    double init_device_pause_duration = 0.0;
    std::vector<TopicConfig> topics;
    std::vector<std::string> default_scripts;
    nlohmann::json custom_params = nlohmann::json::object();
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
