#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/data/data_registry_server.h"

#include <zmq.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

nlohmann::json callRegistry(int port, const nlohmann::json& request) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);
    int timeout_ms = 1000;
    int linger_ms = 0;
    socket.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket.setsockopt(ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket.setsockopt(ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
    socket.connect("tcp://127.0.0.1:" + std::to_string(port));
    const auto text = request.dump();
    zmq::message_t req(text.data(), text.size());
    socket.send(req);
    zmq::message_t reply;
    const auto received = socket.recv(&reply);
    assert(received);
    return nlohmann::json::parse(std::string(static_cast<char*>(reply.data()), reply.size()));
}

}  // namespace

int main() {
    recordlab::host::HostMessageBus bus;
    bus.registerConsumer(recordlab::host::msg::DATA_RECEIVER);
    bus.registerConsumer(recordlab::host::msg::UI);
    constexpr int port = 16679;
    recordlab::host::DataRegistryServer server(bus, "127.0.0.1", port);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto response = callRegistry(port, {
        {"action", "register_data"},
        {"stream", {
            {"data_name", "imu_data"},
            {"data_type", "topic"},
            {"host", "127.0.0.1"},
            {"port", 16510},
            {"node_name", "node"},
            {"encoding", "json"},
            {"parse_mode", "type_vector6_fast"},
            {"ui_max_hz", 60.0},
        }},
    });
    assert(response.value("success", false));

    auto event = bus.waitFor(recordlab::host::msg::DATA_RECEIVER, 1000);
    assert(event.has_value());
    assert(event->type == recordlab::host::msg::DATA_REGISTERED);
    assert(event->payload["stream"]["data_name"] == "imu_data");

    auto list = callRegistry(port, {{"action", "list_data"}});
    assert(list.value("success", false));
    assert(list["streams"].size() == 1);

    auto removed = callRegistry(port, {
        {"action", "unregister_data"},
        {"stream", {{"data_name", "imu_data"}, {"port", 16510}, {"node_name", "node"}}},
    });
    assert(removed.value("success", false));
    auto remove_event = bus.waitFor(recordlab::host::msg::DATA_RECEIVER, 1000);
    assert(remove_event.has_value());
    assert(remove_event->type == recordlab::host::msg::DATA_UNREGISTERED);
    server.stop();

    std::cout << "data registry server ok\n";
    return 0;
}
