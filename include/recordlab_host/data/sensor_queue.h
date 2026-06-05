#pragma once

#include <chrono>
#include <array>
#include <mutex>
#include <optional>
#include <deque>
#include <set>
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
    using CurveSample = std::array<double, 4>;

    void update(const std::string& topic_name, const nlohmann::json& value, double frequency_hz);
    void remove(const std::string& topic_name);
    void clear();
    std::optional<TopicSnapshot> latest(const std::string& topic_name) const;
    std::vector<TopicSnapshot> allLatest() const;
    void setSubscribedNames(std::vector<std::string> names);
    std::vector<std::string> subscribedNames() const;
    void appendCurveSample(const std::string& key,
                           const nlohmann::json& display_values,
                           double timestamp_seconds,
                           std::size_t max_samples = 600);
    std::deque<CurveSample> curveBuffer(const std::string& key) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TopicSnapshot> snapshots_;
    std::set<std::string> subscribed_names_;
    std::unordered_map<std::string, std::deque<CurveSample>> curve_buffers_;
};

}  // namespace recordlab::host
