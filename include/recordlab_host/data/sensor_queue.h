#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace recordlab::host {

class SensorQueue {
public:
    struct TopicSnapshot {
        std::string topic_name;
        nlohmann::json value;
        double frequency_hz = 0.0;
        std::chrono::steady_clock::time_point last_update;
    };

    void update(const std::string& topic_name, const nlohmann::json& value, double frequency_hz);
    std::optional<TopicSnapshot> latest(const std::string& topic_name) const;
    std::vector<TopicSnapshot> allLatest() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TopicSnapshot> snapshots_;
};

}  // namespace recordlab::host
