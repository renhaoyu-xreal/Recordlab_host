#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace recordlab::host {

class EchoTopicSubscriber {
public:
    using Callback = std::function<void(const nlohmann::json&)>;

    EchoTopicSubscriber(std::string host, int port, std::string topic, Callback callback);
    ~EchoTopicSubscriber();

    EchoTopicSubscriber(const EchoTopicSubscriber&) = delete;
    EchoTopicSubscriber& operator=(const EchoTopicSubscriber&) = delete;

    bool pollOnce(int timeout_ms);

private:
    void* context_ = nullptr;
    void* socket_ = nullptr;
    std::string topic_;
    Callback callback_;
};

}  // namespace recordlab::host
