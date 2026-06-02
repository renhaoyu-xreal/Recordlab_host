#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace recordlab::host {

struct HostMessage {
    std::string request_id;
    std::string source;
    std::string target;
    std::string type;
    nlohmann::json payload = nlohmann::json::object();
    /// Optional latest-only key. Messages with the same target and key are
    /// coalesced so slow consumers observe only the newest sample.
    std::string coalesce_key;
};

class HostMessageBus {
public:
    void registerConsumer(const std::string& target);
    void publish(HostMessage message);
    std::optional<HostMessage> waitFor(const std::string& target, int timeout_ms);

    /// Non-blocking: drain up to max_count pending messages for the given target.
    std::vector<HostMessage> drainFor(const std::string& target, int max_count = 256);

    std::size_t queueSize(const std::string& target) const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::string, std::deque<HostMessage>> queues_;
};

}  // namespace recordlab::host
