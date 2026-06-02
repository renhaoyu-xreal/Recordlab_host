#include "recordlab_host/bus/host_message_bus.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    {
        recordlab::host::HostMessageBus bus;
        bus.registerConsumer("agent_manager");
        std::thread producer([&]() {
            bus.publish(recordlab::host::HostMessage{
                "req-1",
                "ui",
                "agent_manager",
                "agent_command",
                {{"cmd", "check"}},
            });
        });
        auto message = bus.waitFor("agent_manager", 1000);
        producer.join();
        assert(message.has_value());
        assert(message->request_id == "req-1");
        assert(message->payload["cmd"] == "check");
        assert(bus.queueSize("agent_manager") == 0);
    }

    {
        recordlab::host::HostMessageBus bus;
        bus.registerConsumer("data_receiver");
        constexpr int producer_count = 8;
        constexpr int per_producer = 50;
        std::vector<std::thread> producers;
        for (int p = 0; p < producer_count; ++p) {
            producers.emplace_back([&, p]() {
                for (int i = 0; i < per_producer; ++i) {
                    bus.publish(recordlab::host::HostMessage{
                        std::to_string(p) + "-" + std::to_string(i),
                        "producer",
                        "data_receiver",
                        "data",
                        {{"value", i}},
                    });
                }
            });
        }
        int received = 0;
        while (received < producer_count * per_producer) {
            auto message = bus.waitFor("data_receiver", 2000);
            assert(message.has_value());
            ++received;
        }
        for (auto& thread : producers) {
            thread.join();
        }
        assert(bus.queueSize("data_receiver") == 0);
    }

    {
        recordlab::host::HostMessageBus bus;
        bus.registerConsumer("empty");
        auto message = bus.waitFor("empty", 10);
        assert(!message.has_value());
    }

    {
        recordlab::host::HostMessageBus bus;
        bus.registerConsumer("ui");
        for (int i = 0; i < 20; ++i) {
            bus.publish(recordlab::host::HostMessage{
                "",
                "data_receiver",
                "ui",
                "topic_data",
                {{"topic_name", "camera_data"}, {"seq", i}},
                "topic_data:camera_data",
            });
        }
        assert(bus.queueSize("ui") == 1);
        auto messages = bus.drainFor("ui");
        assert(messages.size() == 1);
        assert(messages.front().payload["seq"] == 19);
    }
    std::cout << "host message bus ok\n";
    return 0;
}
