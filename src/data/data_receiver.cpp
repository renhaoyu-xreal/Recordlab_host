#include "recordlab_host/data/data_receiver.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <algorithm>
#include <cmath>
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

std::size_t estimateJsonBinaryBytes(const nlohmann::json& value) {
    if (value.is_object()) {
        const auto marker = value.find("__echo_bytes_base64__");
        if (marker != value.end() && marker->is_string()) {
            return marker->get<std::string>().size() * 3 / 4;
        }
    }
    if (value.is_string()) {
        return value.get<std::string>().size() * 3 / 4;
    }
    if (value.is_array()) {
        return value.size();
    }
    return 0;
}

std::size_t estimatePayloadBytes(const nlohmann::json& value) {
    if (value.is_object()) {
        const auto cam_data = value.find("cam_data");
        if (cam_data != value.end() && cam_data->is_object()) {
            std::size_t total = 0;
            for (const auto& [_, cam_info] : cam_data->items()) {
                if (!cam_info.is_object()) continue;
                const auto image = cam_info.find("image");
                if (image == cam_info.end() || !image->is_object()) continue;
                if (image->value("shm", false) || image->contains("shm_seq")) {
                    total += image->dump().size();
                    continue;
                }
                if (image->contains("data")) {
                    total += estimateJsonBinaryBytes((*image)["data"]);
                } else {
                    total += static_cast<std::size_t>(image->value("bytes_per_line", 0))
                        * static_cast<std::size_t>(image->value("height", 0));
                }
            }
            return total;
        }
    }
    return value.dump().size();
}

double secondsFromTimestampValue(double timestamp) {
    if (timestamp <= 0.0) {
        return 0.0;
    }
    return timestamp > 1e12 ? timestamp / 1e9 : timestamp;
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
            topic_states_[topic.name] = {
                std::chrono::steady_clock::now(),
                std::chrono::steady_clock::now(),
                std::chrono::steady_clock::now(),
                topic.ui_max_hz,
                topic.qos.value("debug_stats", false),
                true,
            };
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
}

const SensorQueue& DataReceiver::sensorQueue() const {
    return sensor_queue_;
}

void DataReceiver::onTopicData(const std::string& topic_name, const nlohmann::json& value) {
    if (!accepting_data_.load(std::memory_order_acquire)) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    double frequency_hz = 0.0;
    bool should_publish = false;
    bool is_first = false;
    nlohmann::json stream_frequencies = nlohmann::json::object();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = topic_states_.find(topic_name);
        if (it == topic_states_.end()) return;

        auto& state = it->second;
        state.receive_count += 1;

        // Frequency estimation (instantaneous 1/dt).
        if (!state.first_message) {
            const double delta = std::chrono::duration<double>(now - state.last_receive).count();
            if (delta > 0.0) {
                frequency_hz = 1.0 / delta;
            }
        }
        is_first = state.first_message;
        state.first_message = false;
        state.last_receive = now;

        const std::string stream_key = streamKeyFor(topic_name, value);
        const double stream_ts = streamTimestampFor(topic_name, value, now);
        auto& times = state.stream_receive_times[stream_key];
        if (!times.empty() && stream_ts < times.back() - 1.0) {
            times.clear();
            state.stream_frequencies_hz[stream_key] = 0.0;
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
                    state.stream_frequencies_hz[stream_key] = static_cast<double>(n - 1) / span;
                }
            }
            if (times.size() > 5000) {
                times.erase(times.begin(), times.end() - 2000);
            }
        }
        frequency_hz = state.stream_frequencies_hz[stream_key];
        stream_frequencies = frequenciesToJson(state.stream_frequencies_hz);

        // UI rate-limiting.
        if (state.ui_max_hz <= 0.0) {
            should_publish = true;
        } else {
            const double min_interval = 1.0 / state.ui_max_hz;
            const double elapsed = std::chrono::duration<double>(now - state.last_ui_publish).count();
            if (elapsed >= min_interval || is_first) {
                should_publish = true;
                state.last_ui_publish = now;
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
                        + " payload_bytes_est=" + std::to_string(estimatePayloadBytes(value))
                        + " ui_max_hz=" + std::to_string(state.ui_max_hz));
                state.last_debug_log = now;
                state.receive_count = 0;
                state.publish_count = 0;
            }
        }
    }

    // Always update the sensor queue with the very latest data.
    sensor_queue_.update(topic_name, value, frequency_hz);

    // Publish a rate-limited notification to the bus for the UI consumer.
    if (should_publish) {
        bus_.publish({
            .request_id = {},
            .source = "data_receiver",
            .target = msg::UI,
            .type = msg::TOPIC_DATA,
            .payload = {
                {"topic_name", topic_name},
                {"value", value},
                {"frequency_hz", frequency_hz},
                {"stream_frequencies_hz", stream_frequencies},
                {"first_message", is_first},
            },
            .coalesce_key = "topic_data:" + topic_name,
        });
    }
}

std::string DataReceiver::streamKeyFor(const std::string& topic_name, const nlohmann::json& value) const {
    if (value.is_object()) {
        const auto type = value.find("type");
        if (type != value.end() && type->is_number_integer()) {
            return topic_name + ":" + std::to_string(type->get<int>());
        }
    }
    return topic_name;
}

double DataReceiver::streamTimestampFor(const std::string& topic_name,
                                        const nlohmann::json& value,
                                        std::chrono::steady_clock::time_point now) const {
    (void)topic_name;
    if (value.is_object()) {
        const auto timestamp_ns = value.find("timestamp_ns");
        if (timestamp_ns != value.end() && timestamp_ns->is_number()) {
            const double seconds = timestamp_ns->get<double>() / 1e9;
            if (seconds > 0.0) {
                return seconds;
            }
        }
        const auto timestamp = value.find("timestamp");
        if (timestamp != value.end() && timestamp->is_number()) {
            const double seconds = secondsFromTimestampValue(timestamp->get<double>());
            if (seconds > 0.0) {
                return seconds;
            }
        }
    }
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

}  // namespace recordlab::host
