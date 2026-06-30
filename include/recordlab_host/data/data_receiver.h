#pragma once

#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/communication/echo_topic_subscriber.h"
#include "recordlab_host/data/data_registry_server.h"
#include "recordlab_host/data/sensor_queue.h"
#include "recordlab_host/data/topic_parser.h"

#include <chrono>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace recordlab::host {

/// DataReceiver (PLAN.md T3) — owns topic subscriptions, computes frequency
/// statistics, applies UI rate-limiting, updates SensorQueue, and publishes
/// rate-limited data_update events to the HostMessageBus.
class DataReceiver {
public:
    struct NodeCookie {
        std::string key;
        nlohmann::json value;
        bool is_display = false;
        std::string source_topic;
    };

    struct TopicConfig {
        std::string name;
        int port = 0;
        std::string encoding = "json";
        std::string parse_mode = "json";
        double ui_max_hz = 30.0;  ///< Max rate for UI bus notifications. 0 = unlimited.
        nlohmann::json qos = nlohmann::json::object();
        nlohmann::json metadata = nlohmann::json::object();
    };

    explicit DataReceiver(HostMessageBus& bus);
    ~DataReceiver();

    DataReceiver(const DataReceiver&) = delete;
    DataReceiver& operator=(const DataReceiver&) = delete;

    /// Subscribe to a set of topics. Clears any existing subscriptions first.
    void subscribe(const std::string& host, const std::vector<TopicConfig>& topics);
    void registerDataStream(const DataStreamRegistration& registration);
    void unregisterDataStream(const DataStreamRegistration& registration);

    /// Unsubscribe and release all subscriber resources.
    void unsubscribeAll();

    /// Access the sensor queue for direct latest-value queries.
    const SensorQueue& sensorQueue() const;
    nlohmann::json cookies() const;

private:
    struct TopicState {
        std::chrono::steady_clock::time_point last_receive;
        std::chrono::steady_clock::time_point last_ui_publish;
        std::chrono::steady_clock::time_point last_debug_log;
        double ui_max_hz = 30.0;
        bool debug_stats = false;
        bool is_cookie_topic = false;
        bool first_message = true;
        std::size_t receive_count = 0;
        std::size_t publish_count = 0;
        std::unordered_map<std::string, std::deque<double>> stream_receive_times;
        std::unordered_map<std::string, double> stream_frequencies_hz;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> stream_last_ui_publish;
        std::unique_ptr<TopicParser> parser;
    };

    void onTopicData(const std::string& topic_name, const nlohmann::json& value);
    void subscribeOne(const DataStreamRegistration& registration);
    void refreshSubscribedNamesLocked();
    nlohmann::json updateCookiesLocked(const std::string& topic_name, const nlohmann::json& value);
    nlohmann::json cookiesLocked() const;

    HostMessageBus& bus_;
    SensorQueue sensor_queue_;
    std::vector<std::unique_ptr<EchoTopicSubscriber>> subscribers_;
    std::unordered_map<std::string, std::unique_ptr<EchoTopicSubscriber>> dynamic_subscribers_;
    std::unordered_map<std::string, TopicState> topic_states_;
    std::unordered_map<std::string, std::unordered_set<std::string>> topic_subscription_keys_;
    std::unordered_map<std::string, NodeCookie> cookies_;
    mutable std::mutex mutex_;
    std::atomic<bool> accepting_data_{false};
};

}  // namespace recordlab::host
