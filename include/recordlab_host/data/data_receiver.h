#pragma once

#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/communication/echo_topic_subscriber.h"
#include "recordlab_host/data/sensor_queue.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace recordlab::host {

/// DataReceiver (PLAN.md T3) — owns topic subscriptions, computes frequency
/// statistics, applies UI rate-limiting, updates SensorQueue, and publishes
/// rate-limited data_update events to the HostMessageBus.
class DataReceiver {
public:
    struct TopicConfig {
        std::string name;
        int port = 0;
        std::string encoding = "json";
        double ui_max_hz = 30.0;  ///< Max rate for UI bus notifications. 0 = unlimited.
    };

    explicit DataReceiver(HostMessageBus& bus);
    ~DataReceiver();

    DataReceiver(const DataReceiver&) = delete;
    DataReceiver& operator=(const DataReceiver&) = delete;

    /// Subscribe to a set of topics. Clears any existing subscriptions first.
    void subscribe(const std::string& host, const std::vector<TopicConfig>& topics);

    /// Unsubscribe and release all subscriber resources.
    void unsubscribeAll();

    /// Access the sensor queue for direct latest-value queries.
    const SensorQueue& sensorQueue() const;

private:
    struct TopicState {
        std::chrono::steady_clock::time_point last_receive;
        std::chrono::steady_clock::time_point last_ui_publish;
        double ui_max_hz = 30.0;
        bool first_message = true;
    };

    void onTopicData(const std::string& topic_name, const nlohmann::json& value);

    HostMessageBus& bus_;
    SensorQueue sensor_queue_;
    std::vector<std::unique_ptr<EchoTopicSubscriber>> subscribers_;
    std::unordered_map<std::string, TopicState> topic_states_;
    std::mutex mutex_;
};

}  // namespace recordlab::host
