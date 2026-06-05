#include "recordlab_host/data/sensor_queue.h"

namespace recordlab::host {

void SensorQueue::update(const std::string& topic_name, const nlohmann::json& value, double frequency_hz) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_[topic_name] = {topic_name, value, frequency_hz, std::chrono::steady_clock::now()};
}

void SensorQueue::remove(const std::string& topic_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_.erase(topic_name);
    subscribed_names_.erase(topic_name);
}

void SensorQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_.clear();
    subscribed_names_.clear();
    curve_buffers_.clear();
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

void SensorQueue::setSubscribedNames(std::vector<std::string> names) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribed_names_.clear();
    for (auto& name : names) {
        if (!name.empty()) subscribed_names_.insert(std::move(name));
    }
}

std::vector<std::string> SensorQueue::subscribedNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {subscribed_names_.begin(), subscribed_names_.end()};
}

void SensorQueue::appendCurveSample(const std::string& key,
                                    const nlohmann::json& display_values,
                                    double timestamp_seconds,
                                    std::size_t max_samples) {
    if (key.empty() || !display_values.is_array() || display_values.empty()) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (display_values.size() > 0 && display_values[0].is_number()) x = display_values[0].get<double>();
    if (display_values.size() > 1 && display_values[1].is_number()) y = display_values[1].get<double>();
    if (display_values.size() > 2 && display_values[2].is_number()) z = display_values[2].get<double>();
    std::lock_guard<std::mutex> lock(mutex_);
    auto& buffer = curve_buffers_[key];
    const double ts = timestamp_seconds > 0.0
        ? timestamp_seconds
        : (buffer.empty() ? 0.0 : buffer.back()[0] + 0.01);
    buffer.push_back({ts, x, y, z});
    while (buffer.size() > max_samples) {
        buffer.pop_front();
    }
}

std::deque<SensorQueue::CurveSample> SensorQueue::curveBuffer(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = curve_buffers_.find(key);
    return it == curve_buffers_.end() ? std::deque<CurveSample>{} : it->second;
}

}  // namespace recordlab::host
