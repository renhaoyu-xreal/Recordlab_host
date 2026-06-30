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

    {
        recordlab::host::HostMessageBus bus;
        recordlab::host::DataReceiver receiver(bus);
        echo::Publisher publisher("node_cookie");

        receiver.subscribe("127.0.0.1", {
            recordlab::host::DataReceiver::TopicConfig{
                "node_cookie",
                publisher.getPort(),
                "json",
                "json",
                1000.0,
                nlohmann::json::object(),
                nlohmann::json{{"role", "host_cookie"}},
            },
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        publisher.publishRaw(R"({"key":"FSN","value":"ABC123","isDisplay":true})");
        publisher.publishRaw(R"({"cookies":[{"key":"hidden","value":"internal","is_display":false}]})");

        nlohmann::json cookies;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            auto msg = bus.waitFor(recordlab::host::msg::UI, 100);
            if (!msg || msg->type != recordlab::host::msg::NODE_COOKIES) {
                continue;
            }
            cookies = msg->payload.value("cookies", nlohmann::json::array());
            bool saw_fsn = false;
            bool saw_hidden = false;
            for (const auto& item : cookies) {
                if (item.value("key", std::string{}) == "FSN") {
                    saw_fsn = item.value("is_display", false) && item.value("value", std::string{}) == "ABC123";
                }
                if (item.value("key", std::string{}) == "hidden") {
                    saw_hidden = !item.value("is_display", true);
                }
            }
            if (saw_fsn && saw_hidden) {
                break;
            }
        }
        assert(receiver.cookies().value("cookies", nlohmann::json::array()).size() == 2);
    }

    {
        recordlab::host::HostMessageBus bus;
        recordlab::host::DataReceiver receiver(bus);
        echo::Publisher first_publisher("imu_data");
        echo::Publisher second_publisher("imu_data");

        recordlab::host::DataStreamRegistration first_registration;
        first_registration.data_name = "imu_data";
        first_registration.data_type = "topic";
        first_registration.host = "127.0.0.1";
        first_registration.port = first_publisher.getPort();
        first_registration.node_name = "first_node";
        first_registration.encoding = "json";
        first_registration.parse_mode = "json";
        first_registration.ui_max_hz = 1000.0;
        first_registration.qos = nlohmann::json::object();
        first_registration.metadata = nlohmann::json::object();
        recordlab::host::DataStreamRegistration second_registration;
        second_registration.data_name = "imu_data";
        second_registration.data_type = "topic";
        second_registration.host = "127.0.0.1";
        second_registration.port = second_publisher.getPort();
        second_registration.node_name = "second_node";
        second_registration.encoding = "json";
        second_registration.parse_mode = "json";
        second_registration.ui_max_hz = 1000.0;
        second_registration.qos = nlohmann::json::object();
        second_registration.metadata = nlohmann::json::object();

        receiver.registerDataStream(first_registration);
        receiver.registerDataStream(second_registration);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        first_publisher.publishRaw(R"({"type":11,"timestamp_ns":1000000000,"data":[1,2,3]})");
        second_publisher.publishRaw(R"({"type":22,"timestamp_ns":1000001000,"data":[4,5,6]})");

        bool saw_second_before_unregister = false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !saw_second_before_unregister) {
            auto msg = bus.waitFor(recordlab::host::msg::UI, 100);
            if (!msg || msg->type != recordlab::host::msg::TOPIC_DATA) {
                continue;
            }
            const auto& value = msg->payload["value"];
            saw_second_before_unregister = value.is_object() && value.value("type", 0) == 22;
        }
        assert(saw_second_before_unregister);

        receiver.unregisterDataStream(first_registration);
        second_publisher.publishRaw(R"({"type":22,"timestamp_ns":1000002000,"data":[7,8,9]})");

        bool saw_second_after_unregister = false;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !saw_second_after_unregister) {
            auto msg = bus.waitFor(recordlab::host::msg::UI, 100);
            if (!msg || msg->type != recordlab::host::msg::TOPIC_DATA) {
                continue;
            }
            const auto& value = msg->payload["value"];
            saw_second_after_unregister =
                value.is_object() && value.value("type", 0) == 22
                && value.contains("data") && value["data"].is_array()
                && value["data"].size() >= 3 && value["data"][0] == 7;
        }
        assert(saw_second_after_unregister);
        assert(receiver.sensorQueue().latest("imu_data").has_value());
    }

    std::cout << "data receiver streams ok\n";
    return 0;
}
