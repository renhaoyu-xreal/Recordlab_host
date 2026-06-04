#include "recordlab_host/agents/agent_config_loader.h"

#include <fstream>
#include <stdexcept>

namespace recordlab::host {

AgentConfigLoader::AgentConfigLoader(std::string path) : path_(std::move(path)) {}

nlohmann::json AgentConfigLoader::loadJson() const {
    std::ifstream input(path_);
    if (!input) {
        throw std::runtime_error("Failed to open agents config: " + path_);
    }
    nlohmann::json doc;
    input >> doc;
    return doc;
}

AgentConfig AgentConfigLoader::loadAgent(const std::string& agent_name) const {
    auto doc = loadJson();
    if (!doc.contains("agents") || !doc["agents"].contains(agent_name)) {
        throw std::runtime_error("Agent not found: " + agent_name);
    }
    const auto& item = doc["agents"][agent_name];
    AgentConfig cfg;
    cfg.name = item.value("name", agent_name);
    cfg.node_class = item.at("node_class").get<std::string>();
    cfg.process_type = item.value("process_type", "python_node");
    cfg.subnode_host = item.value("subnode_host", "127.0.0.1");
    cfg.action_name = item.value("action_name", cfg.name + "_actions");
    cfg.goal_port = item.at("goal_port").get<int>();
    cfg.feedback_port = item.at("feedback_port").get<int>();
    cfg.data_port = item.at("data_port").get<int>();
    cfg.root_path = item.value("root_path", "data");
    cfg.init_device_params = item.value("init_device_params", nlohmann::json::object());
    cfg.init_device_pause_duration = item.value("init_device_pause_duration", 0.0);
    cfg.custom_params = item.value("custom_params", nlohmann::json::object());
    for (const auto& script : item.value("default_scripts", nlohmann::json::array())) {
        cfg.default_scripts.push_back(script.get<std::string>());
    }
    for (const auto& topic : item.value("topics", nlohmann::json::array())) {
        if (topic.contains("port")) {
            const auto topic_name = topic.value("name", std::string{"<unknown>"});
            throw std::runtime_error(
                "topics[].port is no longer supported; use agent data_port instead: " +
                cfg.name + "/" + topic_name);
        }
        cfg.topics.push_back(TopicConfig{
            topic.at("name").get<std::string>(),
            topic.value("encoding", "json"),
        });
    }
    return cfg;
}

std::vector<std::string> AgentConfigLoader::loadPrimaryAgents() const {
    auto doc = loadJson();
    std::vector<std::string> result;
    for (const auto& name : doc.value("primary_agents", nlohmann::json::array())) {
        result.push_back(name.get<std::string>());
    }
    return result;
}

}  // namespace recordlab::host
