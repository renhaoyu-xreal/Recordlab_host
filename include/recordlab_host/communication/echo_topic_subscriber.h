#pragma once

#include <functional>
#include <memory>
#include <string>

#include <subscriber.h>
#include <nlohmann/json.hpp>

namespace recordlab::host {

class EchoTopicSubscriber {
public:
    using Callback = std::function<void(const nlohmann::json&)>;

    EchoTopicSubscriber(std::string host, int port, std::string topic, Callback callback);
    EchoTopicSubscriber(std::string host, int port, std::string topic, std::string encoding, Callback callback);
    ~EchoTopicSubscriber();

    EchoTopicSubscriber(const EchoTopicSubscriber&) = delete;
    EchoTopicSubscriber& operator=(const EchoTopicSubscriber&) = delete;

    bool pollOnce(int timeout_ms);

private:
    std::unique_ptr<echo::Subscriber> subscriber_;
    std::string topic_;
    std::string encoding_;
    Callback callback_;
};

}  // namespace recordlab::host
