#include "recordlab_host/communication/echo_topic_subscriber.h"

#include <chrono>
#include <thread>

namespace recordlab::host {

EchoTopicSubscriber::EchoTopicSubscriber(std::string host, int port, std::string topic, Callback callback)
    : topic_(std::move(topic)), callback_(std::move(callback)) {
    subscriber_ = std::make_unique<echo::Subscriber>(
        topic_, host, port,
        [this](const std::string& payload) {
            callback_(nlohmann::json::parse(payload));
        },
        true);
}

EchoTopicSubscriber::~EchoTopicSubscriber() {
    subscriber_.reset();
}

bool EchoTopicSubscriber::pollOnce(int timeout_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return false;
}

}  // namespace recordlab::host
