#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/lifecycle/watchdog.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using recordlab::host::HostMessage;
using recordlab::host::HostMessageBus;
namespace msg = recordlab::host::msg;

HostMessage makeResult(const HostMessage& request, bool success) {
    const std::string request_id = request.request_id.empty()
        ? request.payload.value("request_id", std::string{})
        : request.request_id;
    const std::string cmd = request.type == msg::INIT_DEVICE
        ? "init_device"
        : request.payload.value("cmd", std::string{});
    return {
        .request_id = request_id,
        .source = msg::AGENT_MANAGER,
        .target = msg::WATCHDOG,
        .type = msg::CMD_RESULT,
        .payload = {
            {"request_id", request_id},
            {"agent_name", request.payload.value("agent_name", std::string{})},
            {"cmd", cmd},
            {"success", success},
            {"message", success ? "ok" : "failed"},
        },
    };
}

std::string waitForState(HostMessageBus& bus, const std::string& state, int timeout_ms = 6000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string last_state;
    while (std::chrono::steady_clock::now() < deadline) {
        auto item = bus.waitFor(msg::UI, 200);
        if (!item || item->type != msg::WATCHDOG_STATE) continue;
        last_state = item->payload.value("state", std::string{});
        if (last_state == state) {
            return item->payload.value("reason", std::string{});
        }
    }
    assert(false && "timed out waiting for watchdog state");
    return {};
}

void waitForStateReason(HostMessageBus& bus, const std::string& state,
                        const std::string& reason, int timeout_ms = 9000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto item = bus.waitFor(msg::UI, 200);
        if (!item || item->type != msg::WATCHDOG_STATE) continue;
        if (item->payload.value("state", std::string{}) == state &&
            item->payload.value("reason", std::string{}) == reason) {
            return;
        }
    }
    assert(false && "timed out waiting for watchdog state reason");
}

void runScenario(const std::string& name,
                 const std::function<void(HostMessageBus&, std::atomic<bool>&)>& responder,
                 const std::function<void(HostMessageBus&)>& assertions) {
    HostMessageBus bus;
    bus.registerConsumer(msg::UI);
    std::atomic<bool> running{true};
    std::thread responder_thread([&]() { responder(bus, running); });

    recordlab::host::Watchdog watchdog(bus, name);
    watchdog.start();
    assertions(bus);
    watchdog.stop();
    running = false;
    bus.publish({.target = msg::AGENT_MANAGER, .type = "test_shutdown"});
    responder_thread.join();
}

void replySequence(HostMessageBus& bus, std::atomic<bool>& running,
                   const std::vector<bool>& replies,
                   bool send_wrong_first_result = false) {
    std::size_t index = 0;
    while (running && index < replies.size()) {
        auto request = bus.waitFor(msg::AGENT_MANAGER, 200);
        if (!request) continue;
        if (request->type != msg::CMD_REQUEST && request->type != msg::INIT_DEVICE) continue;
        if (send_wrong_first_result && index == 0) {
            auto wrong = makeResult(*request, false);
            wrong.request_id = "wrong_request_id";
            wrong.payload["request_id"] = "wrong_request_id";
            bus.publish(std::move(wrong));
        }
        bus.publish(makeResult(*request, replies[index]));
        ++index;
    }
}

}  // namespace

int main() {
    runScenario(
        "healthy_agent",
        [](HostMessageBus& bus, std::atomic<bool>& running) {
            replySequence(bus, running, {true, true, true});
        },
        [](HostMessageBus& bus) {
            waitForState(bus, "DISCONNECTED");
            waitForState(bus, "INITIALIZING");
            waitForState(bus, "HEALTHY");
        });

    runScenario(
        "error_agent",
        [](HostMessageBus& bus, std::atomic<bool>& running) {
            replySequence(bus, running, {true, false, true, false, true, false, true});
        },
        [](HostMessageBus& bus) {
            waitForState(bus, "INITIALIZING");
            waitForState(bus, "ERROR", 9000);
            waitForStateReason(bus, "ERROR", "error_check_still_succeeds");
        });

    runScenario(
        "disconnect_after_error",
        [](HostMessageBus& bus, std::atomic<bool>& running) {
            replySequence(bus, running, {true, false, true, false, true, false, false});
        },
        [](HostMessageBus& bus) {
            waitForState(bus, "INITIALIZING");
            waitForState(bus, "ERROR", 9000);
            waitForState(bus, "DISCONNECTED", 10000);
        });

    runScenario(
        "request_match_agent",
        [](HostMessageBus& bus, std::atomic<bool>& running) {
            replySequence(bus, running, {true, true, true}, true);
        },
        [](HostMessageBus& bus) {
            waitForState(bus, "INITIALIZING");
            waitForState(bus, "HEALTHY");
        });

    std::cout << "watchdog state machine ok\n";
    return 0;
}
