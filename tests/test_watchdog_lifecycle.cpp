#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/lifecycle/watchdog.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

using recordlab::host::HostMessage;
using recordlab::host::HostMessageBus;
using recordlab::host::Watchdog;
namespace msg = recordlab::host::msg;

HostMessage waitForType(HostMessageBus& bus, const std::string& target,
                        const std::string& type, int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto opt = bus.waitFor(target, 100);
        if (opt && opt->type == type) {
            return *opt;
        }
    }
    assert(false && "timed out waiting for message type");
    return {};
}

void publishResult(HostMessageBus& bus, const std::string& cmd, bool success,
                   const std::string& message = "") {
    bus.publish({
        .source = msg::AGENT_MANAGER,
        .target = msg::WATCHDOG,
        .type = msg::CMD_RESULT,
        .payload = {{"cmd", cmd}, {"success", success}, {"message", message}},
    });
}

int main() {
    {
        HostMessageBus bus;
        bus.registerConsumer(msg::AGENT_MANAGER);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("agent");

        auto check = waitForType(bus, msg::AGENT_MANAGER, msg::CMD_REQUEST);
        assert(check.payload.value("agent_name", "") == "agent");
        assert(check.payload.value("cmd", "") == "check");
        publishResult(bus, "check", true);

        auto first = waitForType(bus, msg::AGENT_MANAGER, msg::INIT_DEVICE);
        assert(first.payload.value("agent_name", "") == "agent");
        publishResult(bus, "init_device", true);

        bool saw_initializing = false;
        bool saw_healthy = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !saw_healthy) {
            auto opt = bus.waitFor(msg::UI, 100);
            if (!opt || opt->type != msg::WATCHDOG_STATE) {
                continue;
            }
            const auto state = opt->payload.value("state", "");
            saw_initializing = saw_initializing || state == "INITIALIZING";
            saw_healthy = saw_healthy || state == "HEALTHY";
        }
        assert(saw_initializing);
        assert(saw_healthy);
        watchdog.stop();
    }

    {
        HostMessageBus bus;
        bus.registerConsumer(msg::AGENT_MANAGER);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("agent");

        auto first_check = waitForType(bus, msg::AGENT_MANAGER, msg::CMD_REQUEST);
        assert(first_check.payload.value("cmd", "") == "check");
        publishResult(bus, "check", true);

        for (int attempt = 0; attempt < 3; ++attempt) {
            auto init = waitForType(bus, msg::AGENT_MANAGER, msg::INIT_DEVICE);
            assert(init.payload.value("agent_name", "") == "agent");
            publishResult(bus, "init_device", false, "init failed");
            if (attempt < 2) {
                auto release = waitForType(bus, msg::AGENT_MANAGER, msg::CMD_REQUEST);
                assert(release.payload.value("cmd", "") == "release_device");
                publishResult(bus, "release_device", true);
            }
        }

        bool saw_error = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !saw_error) {
            auto opt = bus.waitFor(msg::UI, 100);
            if (opt && opt->type == msg::WATCHDOG_STATE) {
                saw_error = opt->payload.value("state", "") == "ERROR";
            }
        }
        assert(saw_error);

        auto notification = waitForType(bus, msg::UI, msg::USER_NOTIFICATION);
        assert(notification.payload.value("severity", "") == "critical");
        assert(notification.payload.value("error_code", "") == "INIT_DEVICE_FAILED");
        assert(notification.payload.value("state", "") == "ERROR");

        auto healthy_check = waitForType(bus, msg::AGENT_MANAGER, msg::CMD_REQUEST, 4000);
        assert(healthy_check.payload.value("cmd", "") == "check");
        publishResult(bus, "check", true);

        bool stayed_error = false;
        const auto error_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < error_deadline && !stayed_error) {
            auto opt = bus.waitFor(msg::UI, 100);
            if (opt && opt->type == msg::WATCHDOG_STATE) {
                stayed_error = opt->payload.value("state", "") == "ERROR";
            }
        }
        assert(stayed_error);

        for (int attempt = 0; attempt < Watchdog::kMaxCheckFailures; ++attempt) {
            auto check = waitForType(bus, msg::AGENT_MANAGER, msg::CMD_REQUEST, 4000);
            assert(check.payload.value("cmd", "") == "check");
            publishResult(bus, "check", false, "device missing");
        }

        bool saw_disconnected = false;
        const auto disconnect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < disconnect_deadline && !saw_disconnected) {
            auto opt = bus.waitFor(msg::UI, 100);
            if (opt && opt->type == msg::WATCHDOG_STATE) {
                saw_disconnected = opt->payload.value("state", "") == "DISCONNECTED";
            }
        }
        assert(saw_disconnected);
        watchdog.stop();
    }

    std::cout << "watchdog lifecycle ok\n";
    return 0;
}
