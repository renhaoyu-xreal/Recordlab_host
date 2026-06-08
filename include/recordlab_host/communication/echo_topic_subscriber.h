#pragma once

#include <functional>
#include <memory>
#include <string>

#include <subscriber.h>
#include <nlohmann/json.hpp>

namespace echo {

// Compatibility shim for echo_message_system revisions that do not expose
// SubscriberOptions yet. Host keeps parsing QoS metadata, but the current
// fixed-port Subscriber implementation only accepts topic/host/port/callback.
struct SubscriberOptions {
    int receive_high_water_mark = 0;
    int linger_ms = 0;
    bool deliver_latest_only = false;
    int drain_limit = 4000;
};

}  // namespace echo

namespace recordlab::host {

class EchoTopicSubscriber {
public:
    using Callback = std::function<void(const nlohmann::json&)>;

    EchoTopicSubscriber(std::string host, int port, std::string topic, Callback callback);
    EchoTopicSubscriber(std::string host, int port, std::string topic, std::string encoding, Callback callback);
    EchoTopicSubscriber(std::string host,
                        int port,
                        std::string topic,
                        std::string encoding,
                        std::string parse_mode,
                        echo::SubscriberOptions options,
                        Callback callback);
    ~EchoTopicSubscriber();

    EchoTopicSubscriber(const EchoTopicSubscriber&) = delete;
    EchoTopicSubscriber& operator=(const EchoTopicSubscriber&) = delete;

    bool pollOnce(int timeout_ms);

private:
    std::unique_ptr<echo::Subscriber> subscriber_;
    std::string topic_;
    std::string encoding_;
    std::string parse_mode_;
    Callback callback_;
};

}  // namespace recordlab::host
