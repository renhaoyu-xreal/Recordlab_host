#include "recordlab_host/data/sensor_queue.h"

namespace recordlab::host {

void SensorQueue::update(const std::string& topic_name, const nlohmann::json& value, double frequency_hz) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_[topic_name] = {topic_name, value, frequency_hz, std::chrono::steady_clock::now()};
}

std::optional<SensorQueue::TopicSnapshot> SensorQueue::latest(const std::string& topic_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(topic_name);
    if (it == snapshots_.end()) return std::nullopt;
    return it->second;
}

std::vector<SensorQueue::TopicSnapshot> SensorQueue::allLatest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TopicSnapshot> result;
    result.reserve(snapshots_.size());
    for (const auto& [_, snap] : snapshots_) {
        result.push_back(snap);
    }
    return result;
}

}  // namespace recordlab::host
