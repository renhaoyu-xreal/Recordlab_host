#include "recordlab_host/data/data_receiver.h"
#include "recordlab_host/bus/message_types.h"

namespace recordlab::host {

DataReceiver::DataReceiver(HostMessageBus& bus) : bus_(bus) {
    bus_.registerConsumer(msg::UI);
}

DataReceiver::~DataReceiver() {
    unsubscribeAll();
}

void DataReceiver::subscribe(const std::string& host, const std::vector<TopicConfig>& topics) {
    unsubscribeAll();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& topic : topics) {
            topic_states_[topic.name] = {
                std::chrono::steady_clock::now(),
                std::chrono::steady_clock::now(),
                topic.ui_max_hz,
                true,
            };
        }
    }

    for (const auto& topic : topics) {
        subscribers_.push_back(std::make_unique<EchoTopicSubscriber>(
            host, topic.port, topic.name, topic.encoding,
            [this, name = topic.name](const nlohmann::json& value) {
                onTopicData(name, value);
            }));
    }
}

void DataReceiver::unsubscribeAll() {
    subscribers_.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    topic_states_.clear();
}

const SensorQueue& DataReceiver::sensorQueue() const {
    return sensor_queue_;
}

void DataReceiver::onTopicData(const std::string& topic_name, const nlohmann::json& value) {
    const auto now = std::chrono::steady_clock::now();
    double frequency_hz = 0.0;
    bool should_publish = false;
    bool is_first = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = topic_states_.find(topic_name);
        if (it == topic_states_.end()) return;

        auto& state = it->second;

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
                {"first_message", is_first},
            },
        });
    }
}

}  // namespace recordlab::host
