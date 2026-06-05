#include "recordlab_host/data/data_receiver.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <algorithm>
#include <sstream>

namespace recordlab::host {
namespace {

std::string describeTopics(const std::vector<DataReceiver::TopicConfig>& topics) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << topics[i].name << "@" << topics[i].port
            << "/" << topics[i].encoding
            << " parse_mode=" << topics[i].parse_mode
            << " ui_max_hz=" << topics[i].ui_max_hz
            << " qos=" << topics[i].qos.dump();
    }
    return oss.str();
}

echo::SubscriberOptions subscriberOptionsFromQos(const nlohmann::json& qos) {
    echo::SubscriberOptions options;
    if (!qos.is_object()) {
        return options;
    }
    const std::string history = qos.value("history", std::string{});
    const int depth = qos.value("depth", 0);
    options.receive_high_water_mark = qos.value("receive_hwm", depth);
    options.linger_ms = qos.value("linger_ms", 0);
    options.deliver_latest_only = qos.value("deliver_latest_only", history == "latest");
    options.drain_limit = qos.value("drain_limit", 4000);
    return options;
}

nlohmann::json frequenciesToJson(const std::unordered_map<std::string, double>& frequencies) {
    nlohmann::json result = nlohmann::json::object();
    for (const auto& [key, frequency] : frequencies) {
        result[key] = frequency;
    }
    return result;
}

bool metadataMarksCookie(const nlohmann::json& metadata) {
    if (!metadata.is_object()) {
        return false;
    }
    for (const auto* key : {"role", "kind", "type"}) {
        const auto it = metadata.find(key);
        if (it != metadata.end() && it->is_string() && it->get<std::string>() == "host_cookie") {
            return true;
        }
    }
    return false;
}

bool jsonBoolAlias(const nlohmann::json& value, const char* snake_key, const char* camel_key, bool fallback) {
    if (!value.is_object()) {
        return fallback;
    }
    const auto snake = value.find(snake_key);
    if (snake != value.end() && snake->is_boolean()) {
        return snake->get<bool>();
    }
    const auto camel = value.find(camel_key);
    if (camel != value.end() && camel->is_boolean()) {
        return camel->get<bool>();
    }
    return fallback;
}

}  // namespace

DataReceiver::DataReceiver(HostMessageBus& bus) : bus_(bus) {
    bus_.registerConsumer(msg::UI);
}

DataReceiver::~DataReceiver() {
    unsubscribeAll();
}

void DataReceiver::subscribe(const std::string& host, const std::vector<TopicConfig>& topics) {
    unsubscribeAll();
    accepting_data_.store(false, std::memory_order_release);
    common::Logger::instance().log(
        common::LogLevel::Info,
        "DataReceiver",
        "subscribe host=" + host + ", topics=[" + describeTopics(topics) + "]");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& topic : topics) {
            auto& state = topic_states_[topic.name];
            state = {
                std::chrono::steady_clock::now(),
                std::chrono::steady_clock::now(),
                std::chrono::steady_clock::now(),
                topic.ui_max_hz,
                topic.qos.value("debug_stats", false),
                metadataMarksCookie(topic.metadata),
                true,
            };
            state.parser = createTopicParser(topic.parse_mode, topic.metadata);
        }
    }
    accepting_data_.store(true, std::memory_order_release);

    for (const auto& topic : topics) {
        subscribers_.push_back(std::make_unique<EchoTopicSubscriber>(
            host, topic.port, topic.name, topic.encoding,
            topic.parse_mode,
            subscriberOptionsFromQos(topic.qos),
            [this, name = topic.name](const nlohmann::json& value) {
                onTopicData(name, value);
            }));
    }
}

void DataReceiver::registerDataStream(const DataStreamRegistration& registration) {
    if (registration.data_name.empty() || registration.port <= 0) {
        return;
    }
    subscribeOne(registration);
}

void DataReceiver::unregisterDataStream(const DataStreamRegistration& registration) {
    const std::string key = registration.data_name + "@" + std::to_string(registration.port);
    std::lock_guard<std::mutex> lock(mutex_);
    dynamic_subscribers_.erase(key);
    topic_states_.erase(registration.data_name);
    sensor_queue_.remove(registration.data_name);
}

void DataReceiver::unsubscribeAll() {
    accepting_data_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        topic_states_.clear();
    }
    if (!subscribers_.empty()) {
        common::Logger::instance().log(
            common::LogLevel::Info,
            "DataReceiver",
            "unsubscribe topics count=" + std::to_string(subscribers_.size()));
    }
    subscribers_.clear();
    dynamic_subscribers_.clear();
    sensor_queue_.clear();
}

const SensorQueue& DataReceiver::sensorQueue() const {
    return sensor_queue_;
}

nlohmann::json DataReceiver::cookies() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cookiesLocked();
}

void DataReceiver::onTopicData(const std::string& topic_name, const nlohmann::json& value) {
    if (!accepting_data_.load(std::memory_order_acquire)) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    double frequency_hz = 0.0;
    bool should_publish = false;
    bool is_first = false;
    ParsedTopicSample sample;
    nlohmann::json stream_frequencies = nlohmann::json::object();
    nlohmann::json cookie_payload;
    bool is_cookie_topic = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = topic_states_.find(topic_name);
        if (it == topic_states_.end()) return;

        auto& state = it->second;
        if (!state.parser) {
            state.parser = createTopicParser("json");
        }
        sample = state.parser->parse(topic_name, value, now);
        state.receive_count += 1;
        is_cookie_topic = state.is_cookie_topic;
        if (is_cookie_topic) {
            cookie_payload = updateCookiesLocked(topic_name, sample.value);
        }

        // Frequency estimation (instantaneous 1/dt).
        if (!state.first_message) {
            const double delta = std::chrono::duration<double>(now - state.last_receive).count();
            if (delta > 0.0) {
                frequency_hz = 1.0 / delta;
            }
        }
        const bool topic_first = state.first_message;
        state.first_message = false;
        state.last_receive = now;

        const double stream_ts = sample.timestamp_seconds;
        auto& times = state.stream_receive_times[sample.stream_key];
        is_first = topic_first || times.empty();
        if (!times.empty() && stream_ts < times.back() - 1.0) {
            times.clear();
            state.stream_frequencies_hz[sample.stream_key] = 0.0;
            state.stream_last_ui_publish.erase(sample.stream_key);
            is_first = true;
        }
        if (times.empty() || std::abs(stream_ts - times.back()) >= 1e-6) {
            times.push_back(stream_ts);
            const double cutoff = stream_ts - 1.0;
            while (!times.empty() && times.front() < cutoff) {
                times.pop_front();
            }
            if (times.size() >= 3) {
                const std::size_t n = std::min<std::size_t>(times.size(), 50);
                const double span = times.back() - times[times.size() - n];
                if (span > 0.001) {
                    state.stream_frequencies_hz[sample.stream_key] = static_cast<double>(n - 1) / span;
                }
            }
            if (times.size() > 5000) {
                times.erase(times.begin(), times.end() - 2000);
            }
        }
        frequency_hz = state.stream_frequencies_hz[sample.stream_key];
        stream_frequencies = frequenciesToJson(state.stream_frequencies_hz);

        // UI rate-limiting is per logical stream. A single ROS-style topic can
        // multiplex IMU0/IMU1 substreams by type, and those must not suppress
        // each other's UI updates.
        if (state.ui_max_hz <= 0.0) {
            should_publish = true;
        } else {
            const double min_interval = 1.0 / state.ui_max_hz;
            auto last_publish_it = state.stream_last_ui_publish.find(sample.stream_key);
            const bool stream_never_published = last_publish_it == state.stream_last_ui_publish.end();
            const double elapsed = stream_never_published
                ? min_interval
                : std::chrono::duration<double>(now - last_publish_it->second).count();
            if (elapsed >= min_interval || is_first || stream_never_published) {
                should_publish = true;
                state.stream_last_ui_publish[sample.stream_key] = now;
            }
        }
        if (should_publish) {
            state.publish_count += 1;
        }

        if (state.debug_stats) {
            const double debug_elapsed = std::chrono::duration<double>(now - state.last_debug_log).count();
            if (debug_elapsed >= 1.0) {
                common::Logger::instance().log(
                    common::LogLevel::Debug,
                    "DataReceiver",
                    topic_name + " recv_count=" + std::to_string(state.receive_count)
                        + " ui_publish_count=" + std::to_string(state.publish_count)
                        + " latest_hz=" + std::to_string(frequency_hz)
                        + " payload_bytes_est=" + std::to_string(sample.bytes_estimate)
                        + " ui_max_hz=" + std::to_string(state.ui_max_hz));
                state.last_debug_log = now;
                state.receive_count = 0;
                state.publish_count = 0;
            }
        }
    }

    if (is_cookie_topic) {
        if (!cookie_payload.empty()) {
            bus_.publish({
                .request_id = {},
                .source = "data_receiver",
                .target = msg::UI,
                .type = msg::NODE_COOKIES,
                .payload = std::move(cookie_payload),
                .coalesce_key = "node_cookies",
            });
        }
        return;
    }

    // Always update the sensor queue with the very latest data.
    sensor_queue_.update(topic_name, sample.value, frequency_hz);
    sensor_queue_.appendCurveSample(sample.stream_key, sample.display_values, sample.timestamp_seconds);

    // Publish a rate-limited notification to the bus for the UI consumer.
    if (should_publish) {
        nlohmann::json payload = {
            {"topic_name", topic_name},
            {"stream_key", sample.stream_key},
            {"value", sample.value},
            {"frequency_hz", frequency_hz},
            {"stream_frequencies_hz", stream_frequencies},
            {"display_values", sample.display_values},
            {"ui_signal", sample.ui_signal},
            {"first_message", is_first},
        };
        bus_.publish({
            .request_id = {},
            .source = "data_receiver",
            .target = msg::UI,
            .type = msg::TOPIC_DATA,
            .payload = std::move(payload),
            .coalesce_key = "topic_data:" + sample.stream_key,
        });
    }
}

void DataReceiver::subscribeOne(const DataStreamRegistration& registration) {
    const std::string key = registration.data_name + "@" + std::to_string(registration.port);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& state = topic_states_[registration.data_name];
        state.last_receive = std::chrono::steady_clock::now();
        state.last_ui_publish = std::chrono::steady_clock::now();
        state.last_debug_log = std::chrono::steady_clock::now();
        state.ui_max_hz = registration.ui_max_hz;
        state.debug_stats = registration.qos.value("debug_stats", false);
        state.is_cookie_topic = metadataMarksCookie(registration.metadata);
        state.first_message = true;
        state.parser = createTopicParser(registration.parse_mode, registration.metadata);
        dynamic_subscribers_.erase(key);
    }
    accepting_data_.store(true, std::memory_order_release);
    auto subscriber = std::make_unique<EchoTopicSubscriber>(
        registration.host, registration.port, registration.data_name, registration.encoding,
        registration.parse_mode,
        subscriberOptionsFromQos(registration.qos),
        [this, name = registration.data_name](const nlohmann::json& payload) {
            onTopicData(name, payload);
        });
    std::lock_guard<std::mutex> lock(mutex_);
    dynamic_subscribers_[key] = std::move(subscriber);
}

nlohmann::json DataReceiver::updateCookiesLocked(const std::string& topic_name, const nlohmann::json& value) {
    auto apply_one = [&](const nlohmann::json& item) {
        if (!item.is_object()) {
            return;
        }
        const auto key_it = item.find("key");
        if (key_it == item.end() || !key_it->is_string() || key_it->get<std::string>().empty()) {
            return;
        }
        NodeCookie cookie;
        cookie.key = key_it->get<std::string>();
        const auto value_it = item.find("value");
        cookie.value = value_it == item.end() ? nlohmann::json{} : *value_it;
        cookie.is_display = jsonBoolAlias(item, "is_display", "isDisplay", false);
        cookie.source_topic = topic_name;
        cookies_[cookie.key] = std::move(cookie);
    };

    if (value.is_object()) {
        const auto batch = value.find("cookies");
        if (batch != value.end() && batch->is_array()) {
            for (const auto& item : *batch) {
                apply_one(item);
            }
        } else {
            apply_one(value);
        }
    } else if (value.is_array()) {
        for (const auto& item : value) {
            apply_one(item);
        }
    }
    return cookiesLocked();
}

nlohmann::json DataReceiver::cookiesLocked() const {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [_, cookie] : cookies_) {
        result.push_back({
            {"key", cookie.key},
            {"value", cookie.value},
            {"is_display", cookie.is_display},
            {"source_topic", cookie.source_topic},
        });
    }
    return {{"cookies", result}};
}

}  // namespace recordlab::host
