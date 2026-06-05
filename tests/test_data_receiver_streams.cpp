#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/data/data_receiver.h"

#include <publisher.h>

#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>

int main() {
    {
        recordlab::host::HostMessageBus bus;
        recordlab::host::DataReceiver receiver(bus);
        echo::Publisher publisher("imu_data");

        receiver.subscribe("127.0.0.1", {
            recordlab::host::DataReceiver::TopicConfig{
                "imu_data",
                publisher.getPort(),
                "json",
                "json",
                1000.0,
                nlohmann::json::object(),
            },
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        for (int i = 0; i < 20; ++i) {
            publisher.publishRaw(R"({"type":1,"timestamp_ns":1000000000,"data":[1,2,3]})");
            publisher.publishRaw(R"({"type":2,"timestamp_ns":1000001000,"data":[4,5,6]})");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::set<int> seen_types;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && seen_types.size() < 2) {
            auto msg = bus.waitFor(recordlab::host::msg::UI, 100);
            if (!msg || msg->type != recordlab::host::msg::TOPIC_DATA) {
                continue;
            }
            const auto& value = msg->payload["value"];
            if (value.is_object() && value.contains("type")) {
                seen_types.insert(value.value("type", 0));
            }
        }

        assert(seen_types.count(1) == 1);
        assert(seen_types.count(2) == 1);
    }

    {
        recordlab::host::HostMessageBus bus;
        recordlab::host::DataReceiver receiver(bus);
        echo::Publisher publisher("imu_data");

        receiver.subscribe("127.0.0.1", {
            recordlab::host::DataReceiver::TopicConfig{
                "imu_data",
                publisher.getPort(),
                "json",
                "json",
                1000.0,
                nlohmann::json::object(),
            },
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        for (int i = 0; i < 80; ++i) {
            const long long ts = 1000000000LL + i * 1000000LL;
            publisher.publishRaw(
                "{\"type\":4,\"timestamp_ns\":" + std::to_string(ts) + ",\"data\":[1,2,3]}");
            publisher.publishRaw(
                "{\"type\":5,\"timestamp_ns\":" + std::to_string(ts) + ",\"data\":[4,5,6]}");
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }

        std::map<int, double> latest_hz;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline &&
               (latest_hz[4] < 900.0 || latest_hz[5] < 900.0)) {
            auto msg = bus.waitFor(recordlab::host::msg::UI, 100);
            if (!msg || msg->type != recordlab::host::msg::TOPIC_DATA) {
                continue;
            }
            const auto& value = msg->payload["value"];
            if (!value.is_object() || !value.contains("type")) {
                continue;
            }
            latest_hz[value.value("type", 0)] = msg->payload.value("frequency_hz", 0.0);
        }

        assert(latest_hz[4] > 900.0 && latest_hz[4] < 1100.0);
        assert(latest_hz[5] > 900.0 && latest_hz[5] < 1100.0);
    }

    std::cout << "data receiver streams ok\n";
    return 0;
}
