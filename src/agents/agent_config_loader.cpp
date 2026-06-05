#include "recordlab_host/agents/agent_config_loader.h"

#include <fstream>
#include <stdexcept>

namespace recordlab::host {
namespace {

nlohmann::json resolveSharedValue(const nlohmann::json& doc,
                                  const nlohmann::json& item,
                                  const char* field,
                                  const char* shared_group,
                                  nlohmann::json fallback) {
    const auto value_it = item.find(field);
    if (value_it == item.end()) {
        return fallback;
    }
    if (!value_it->is_string()) {
        throw std::runtime_error(std::string(field) + " must reference shared." + shared_group + " by name");
    }
    const std::string key = value_it->get<std::string>();
    const auto shared_it = doc.find("shared");
    if (shared_it == doc.end() || !shared_it->is_object()) {
        throw std::runtime_error(std::string("Shared config not found for ") + field + ": " + key);
    }
    const auto group_it = shared_it->find(shared_group);
    if (group_it == shared_it->end() || !group_it->is_object()) {
        throw std::runtime_error(std::string("Shared config group not found for ") + field + ": " + shared_group);
    }
    const auto resolved_it = group_it->find(key);
    if (resolved_it == group_it->end()) {
        throw std::runtime_error(std::string("Shared config key not found for ") + field + ": " + key);
    }
    return *resolved_it;
}

}  // namespace

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
    cfg.sensor_layout = resolveSharedValue(
        doc, item, "sensor_layout", "sensor_layouts", nlohmann::json::object());
    cfg.error_messages = resolveSharedValue(
        doc, item, "error_messages", "error_messages", nlohmann::json::object());
    cfg.ui_bindings = resolveSharedValue(
        doc, item, "ui_bindings", "ui_bindings", nlohmann::json::object());
    for (const auto& script : item.value("default_scripts", nlohmann::json::array())) {
        cfg.default_scripts.push_back(script.get<std::string>());
    }
    const auto exposed_commands = resolveSharedValue(
        doc, item, "exposed_commands", "exposed_commands", nlohmann::json::array());
    for (const auto& cmd : exposed_commands) {
        cfg.exposed_commands.push_back(cmd.get<std::string>());
    }
    const auto commands = resolveSharedValue(
        doc, item, "commands", "commands", nlohmann::json::object());
    cfg.commands.default_timeout_ms = commands.value("default_timeout_ms", 5000);
    const auto command_items = commands.value("items", nlohmann::json::array());
    if (command_items.is_array()) {
        for (const auto& command_item : command_items) {
            if (command_item.is_string()) {
                cfg.commands.items.push_back({command_item.get<std::string>(), 0, nlohmann::json::object()});
            } else if (command_item.is_object()) {
                cfg.commands.items.push_back({
                    command_item.value("name", std::string{}),
                    command_item.value("timeout_ms", 0),
                    command_item.value("params_schema", nlohmann::json::object()),
                });
            }
        }
    }
    const auto topics = resolveSharedValue(
        doc, item, "topics", "topic_sets", nlohmann::json::array());
    for (const auto& topic : topics) {
        if (topic.contains("port")) {
            const auto topic_name = topic.value("name", std::string{"<unknown>"});
            throw std::runtime_error(
                "topics[].port is no longer supported; use agent data_port instead: " +
                cfg.name + "/" + topic_name);
        }
        cfg.topics.push_back(TopicConfig{
            topic.at("name").get<std::string>(),
            topic.value("encoding", "json"),
            topic.value("parse_mode", "json"),
            topic.value("ui_max_hz", 30.0),
            topic.value("qos", nlohmann::json::object()),
            topic.value("metadata", nlohmann::json::object()),
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

int AgentConfig::commandTimeoutMs(const std::string& cmd) const {
    for (const auto& item : commands.items) {
        if (item.name == cmd && item.timeout_ms > 0) {
            return item.timeout_ms;
        }
    }
    return commands.default_timeout_ms > 0 ? commands.default_timeout_ms : 5000;
}

}  // namespace recordlab::host
