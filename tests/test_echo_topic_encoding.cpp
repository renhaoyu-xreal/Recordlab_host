#include "recordlab_host/communication/echo_topic_subscriber.h"

#include <publisher.h>

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    echo::Publisher publisher("camera_data");
    std::vector<nlohmann::json> received;
    recordlab::host::EchoTopicSubscriber subscriber(
        "127.0.0.1",
        publisher.getPort(),
        "camera_data",
        "json_binary",
        [&](const nlohmann::json& value) { received.push_back(value); });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (int i = 0; i < 10 && received.empty(); ++i) {
        publisher.publishRaw(R"({"image":{"width":2,"height":1,"data":{"__echo_bytes_base64__":"AQIDBA=="}}})");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    assert(!received.empty());
    assert(received.front()["image"]["data"]["__echo_bytes_base64__"] == "AQIDBA==");
    std::cout << "echo topic encoding ok\n";
    return 0;
}
