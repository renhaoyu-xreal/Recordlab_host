#include "recordlab_host/bus/host_message_bus.h"

namespace recordlab::host {
namespace {

bool matchesPattern(const std::string& value, const std::string& pattern) {
    if (pattern.empty() || pattern == "*") return true;
    if (pattern.back() == '*') {
        return value.rfind(pattern.substr(0, pattern.size() - 1), 0) == 0;
    }
    return value == pattern;
}

void pushMessage(std::map<std::string, std::deque<HostMessage>>& queues, HostMessage message) {
    auto& queue = queues[message.target];
    if (!message.coalesce_key.empty()) {
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->coalesce_key == message.coalesce_key) {
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
    }
    queue.push_back(std::move(message));
}

}  // namespace

void HostMessageBus::registerConsumer(const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.try_emplace(target);
}

void HostMessageBus::subscribe(const std::string& target,
                               const std::string& type_pattern,
                               const std::string& source_pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.try_emplace(target);
    subscriptions_.push_back({target, type_pattern, source_pattern});
}

void HostMessageBus::publish(HostMessage message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> subscription_targets;
        for (const auto& sub : subscriptions_) {
            if (sub.target != message.target
                && matchesPattern(message.type, sub.type_pattern)
                && matchesPattern(message.source, sub.source_pattern)) {
                subscription_targets.push_back(sub.target);
            }
        }
        for (const auto& target : subscription_targets) {
            HostMessage copy = message;
            copy.target = target;
            pushMessage(queues_, std::move(copy));
        }
        pushMessage(queues_, std::move(message));
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
