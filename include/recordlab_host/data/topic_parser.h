#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace recordlab::host {

struct ParsedTopicSample {
    std::string topic_name;
    std::string stream_key;
    nlohmann::json value = nlohmann::json::object();
    nlohmann::json display_values = nlohmann::json::object();
    nlohmann::json ui_signal = nlohmann::json::object();
    double timestamp_seconds = 0.0;
    std::size_t bytes_estimate = 0;
};

class TopicParser {
public:
    virtual ~TopicParser() = default;
    virtual ParsedTopicSample parse(const std::string& topic_name,
                                    const nlohmann::json& raw,
                                    std::chrono::steady_clock::time_point now) const = 0;
};

std::unique_ptr<TopicParser> createTopicParser(const std::string& parse_mode,
                                               const nlohmann::json& metadata = nlohmann::json::object());

}  // namespace recordlab::host
