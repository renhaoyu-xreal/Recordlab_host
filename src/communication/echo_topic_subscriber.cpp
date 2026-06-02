#include "recordlab_host/communication/echo_topic_subscriber.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace recordlab::host {
namespace {

bool extractInt(const std::string& raw, const char* key, int& out) {
    const auto pos = raw.find(key);
    if (pos == std::string::npos) return false;
    const char* p = raw.data() + pos + std::char_traits<char>::length(key);
    while (*p == ' ') ++p;
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    int value = 0;
    bool saw_digit = false;
    while (*p >= '0' && *p <= '9') {
        saw_digit = true;
        value = value * 10 + (*p - '0');
        ++p;
    }
    if (!saw_digit) return false;
    out = negative ? -value : value;
    return true;
}

bool extractInt64(const std::string& raw, const char* key, std::int64_t& out) {
    const auto pos = raw.find(key);
    if (pos == std::string::npos) return false;
    const char* p = raw.data() + pos + std::char_traits<char>::length(key);
    while (*p == ' ') ++p;
    std::int64_t value = 0;
    bool saw_digit = false;
    while (*p >= '0' && *p <= '9') {
        saw_digit = true;
        value = value * 10 + (*p - '0');
        ++p;
    }
    if (!saw_digit) return false;
    out = value;
    return true;
}

bool extractDoubleArray6(const std::string& raw, std::array<double, 6>& values) {
    const auto pos = raw.find("\"data\":[");
    if (pos == std::string::npos) return false;
    const char* p = raw.data() + pos + 8;
    for (int i = 0; i < 6; ++i) {
        while (*p == ' ' || *p == ',') ++p;
        if (*p == ']' || *p == '\0') break;
        char* end = nullptr;
        values[static_cast<std::size_t>(i)] = std::strtod(p, &end);
        if (end == p) return false;
        p = end;
    }
    return true;
}

nlohmann::json parseImuFast(const std::string& raw) {
    int type = 0;
    std::int64_t timestamp_ns = 0;
    std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (!extractInt(raw, "\"type\":", type)
        || !extractInt64(raw, "\"timestamp_ns\":", timestamp_ns)
        || !extractDoubleArray6(raw, values)) {
        return nlohmann::json::parse(raw);
    }
    return nlohmann::json{
        {"type", type},
        {"timestamp_ns", timestamp_ns},
        {"data", {values[0], values[1], values[2], values[3], values[4], values[5]}},
    };
}

}  // namespace

EchoTopicSubscriber::EchoTopicSubscriber(std::string host, int port, std::string topic, Callback callback)
    : EchoTopicSubscriber(std::move(host), port, std::move(topic), "json", std::move(callback)) {}

EchoTopicSubscriber::EchoTopicSubscriber(std::string host, int port, std::string topic, std::string encoding, Callback callback)
    : topic_(std::move(topic)), encoding_(std::move(encoding)), callback_(std::move(callback)) {
    subscriber_ = std::make_unique<echo::Subscriber>(
        topic_, host, port,
        [this](const std::string& payload) {
            if (encoding_ == "json" || encoding_ == "json_binary" || encoding_.empty()) {
                if (topic_ == "imu_data") {
                    callback_(parseImuFast(payload));
                    return;
                }
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
