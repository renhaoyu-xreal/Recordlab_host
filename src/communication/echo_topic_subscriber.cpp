#include "recordlab_host/communication/echo_topic_subscriber.h"

#include <chrono>
#include <thread>

namespace recordlab::host {

EchoTopicSubscriber::EchoTopicSubscriber(std::string host, int port, std::string topic, Callback callback)
    : EchoTopicSubscriber(std::move(host), port, std::move(topic), "json", std::move(callback)) {}

EchoTopicSubscriber::EchoTopicSubscriber(std::string host, int port, std::string topic, std::string encoding, Callback callback)
    : topic_(std::move(topic)), encoding_(std::move(encoding)), callback_(std::move(callback)) {
    subscriber_ = std::make_unique<echo::Subscriber>(
        topic_, host, port,
        [this](const std::string& payload) {
            if (encoding_ == "json" || encoding_ == "json_binary" || encoding_.empty()) {
                callback_(nlohmann::json::parse(payload));
                return;
            }
            callback_(nlohmann::json{
                {"encoding", encoding_},
                {"raw_size", payload.size()},
                {"payload_kind", "raw"},
                {"message", "non-json topic payload received"},
            });
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
