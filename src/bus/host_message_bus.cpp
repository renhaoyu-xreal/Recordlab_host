#include "recordlab_host/bus/host_message_bus.h"

namespace recordlab::host {

void HostMessageBus::registerConsumer(const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.try_emplace(target);
}

void HostMessageBus::publish(HostMessage message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_[message.target].push_back(std::move(message));
    }
    cv_.notify_all();
}

std::optional<HostMessage> HostMessageBus::waitFor(const std::string& target, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto ready = [&]() { return !queues_[target].empty(); };
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready)) {
        return std::nullopt;
    }
    auto message = std::move(queues_[target].front());
    queues_[target].pop_front();
    return message;
}

std::size_t HostMessageBus::queueSize(const std::string& target) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(target);
    return it == queues_.end() ? 0 : it->second.size();
}

std::vector<HostMessage> HostMessageBus::drainFor(const std::string& target, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(target);
    if (it == queues_.end() || it->second.empty()) return {};
    auto& q = it->second;
    const int count = std::min(max_count, static_cast<int>(q.size()));
    std::vector<HostMessage> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.push_back(std::move(q.front()));
        q.pop_front();
    }
    return result;
}

}  // namespace recordlab::host
