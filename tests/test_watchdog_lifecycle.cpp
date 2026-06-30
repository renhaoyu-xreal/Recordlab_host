#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/lifecycle/watchdog.h"

#include <cassert>
#include <chrono>
#include <functional>
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

HostMessage waitForMatching(HostMessageBus& bus, const std::string& target,
                            const std::function<bool(const HostMessage&)>& predicate,
                            int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto opt = bus.waitFor(target, 100);
        if (opt && predicate(*opt)) {
            return *opt;
        }
    }
    assert(false && "timed out waiting for matching message");
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
        bus.registerConsumer(msg::AGENT_MANAGER_HEALTH);
        bus.registerConsumer(msg::AGENT_MANAGER_PRIORITY);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("agent");

        auto check = waitForType(bus, msg::AGENT_MANAGER_HEALTH, msg::CMD_REQUEST);
        assert(check.payload.value("agent_name", "") == "agent");
        assert(check.payload.value("cmd", "") == "check");
        publishResult(bus, "check", true);

        auto first = waitForType(bus, msg::AGENT_MANAGER, msg::INIT_DEVICE);
        assert(first.payload.value("agent_name", "") == "agent");
        publishResult(bus, "init_device", true);

        auto start = waitForType(bus, msg::AGENT_MANAGER_PRIORITY, msg::CMD_REQUEST);
        assert(start.payload.value("agent_name", "") == "agent");
        assert(start.payload.value("cmd", "") == "start_device");
        assert(start.payload.value("silent", false) == true);
        publishResult(bus, "start_device", true);

        bool saw_initializing = false;
        bool saw_healthy = false;
        bool saw_start_log = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !(saw_healthy && saw_start_log)) {
            auto opt = bus.waitFor(msg::UI, 100);
            if (!opt) {
                continue;
            }
            if (opt->type == msg::WATCHDOG_STATE) {
                const auto state = opt->payload.value("state", "");
                saw_initializing = saw_initializing || state == "INITIALIZING";
                saw_healthy = saw_healthy || (state == "HEALTHY" && opt->payload.value("device_started", false));
            } else if (opt->type == msg::LOG_ENTRY) {
                const auto message = opt->payload.value("message", std::string{});
                saw_start_log = saw_start_log || message.find("start_device") != std::string::npos;
            }
        }
        assert(saw_initializing);
        assert(saw_healthy);
        assert(saw_start_log);
        watchdog.stop();
    }

    {
        HostMessageBus bus;
        bus.registerConsumer(msg::AGENT_MANAGER);
        bus.registerConsumer(msg::AGENT_MANAGER_HEALTH);
        bus.registerConsumer(msg::AGENT_MANAGER_PRIORITY);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("agent");

        auto first_check = waitForType(bus, msg::AGENT_MANAGER_HEALTH, msg::CMD_REQUEST);
        assert(first_check.payload.value("cmd", "") == "check");
        publishResult(bus, "check", true);

        for (int attempt = 0; attempt < 3; ++attempt) {
            auto init = waitForType(bus, msg::AGENT_MANAGER, msg::INIT_DEVICE);
            assert(init.payload.value("agent_name", "") == "agent");
            publishResult(bus, "init_device", false, "init failed");
            if (attempt < 2) {
                auto release = waitForType(bus, msg::AGENT_MANAGER_PRIORITY, msg::CMD_REQUEST);
                assert(release.payload.value("cmd", "") == "reboot_device");
                publishResult(bus, "reboot_device", true);
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

        auto healthy_check = waitForType(bus, msg::AGENT_MANAGER_HEALTH, msg::CMD_REQUEST, 4000);
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
            auto check = waitForType(bus, msg::AGENT_MANAGER_HEALTH, msg::CMD_REQUEST, 4000);
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

    {
        HostMessageBus bus;
        bus.registerConsumer(msg::AGENT_MANAGER);
        bus.registerConsumer(msg::AGENT_MANAGER_HEALTH);
        bus.registerConsumer(msg::AGENT_MANAGER_PRIORITY);
        bus.registerConsumer(msg::SCRIPTS_ACTUATOR);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("primary");
        watchdog.setMonitoredAgents({"primary", "remote"}, false);

        auto primary_check = waitForMatching(bus, msg::AGENT_MANAGER_HEALTH, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "primary" &&
                   item.payload.value("cmd", std::string{}) == "check";
        });
        bus.publish({
            .request_id = primary_check.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", primary_check.request_id},
                {"agent_name", "primary"},
                {"cmd", "check"},
                {"success", true},
                {"message", "ok"},
            },
        });

        auto remote_check = waitForMatching(bus, msg::AGENT_MANAGER_HEALTH, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "remote" &&
                   item.payload.value("cmd", std::string{}) == "check";
        });
        bus.publish({
            .request_id = remote_check.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", remote_check.request_id},
                {"agent_name", "remote"},
                {"cmd", "check"},
                {"success", false},
                {"message", "missing"},
            },
        });

        auto stop_script = waitForType(bus, msg::SCRIPTS_ACTUATOR, msg::STOP_SCRIPT);
        assert(stop_script.payload.value("reason", std::string{}).find("remote") != std::string::npos);

        bool saw_primary_stop_record = false;
        bool saw_remote_stop_record = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !(saw_primary_stop_record && saw_remote_stop_record)) {
            auto item = bus.waitFor(msg::AGENT_MANAGER_PRIORITY, 100);
            if (!item || item->type != msg::CMD_REQUEST ||
                item->payload.value("cmd", std::string{}) != "stop_record") {
                continue;
            }
            const auto agent = item->payload.value("agent_name", std::string{});
            saw_primary_stop_record = saw_primary_stop_record || agent == "primary";
            saw_remote_stop_record = saw_remote_stop_record || agent == "remote";
        }
        assert(saw_primary_stop_record);
        assert(saw_remote_stop_record);
        watchdog.stop();
    }

    {
        HostMessageBus bus;
        bus.registerConsumer(msg::AGENT_MANAGER);
        bus.registerConsumer(msg::AGENT_MANAGER_HEALTH);
        bus.registerConsumer(msg::AGENT_MANAGER_PRIORITY);
        bus.registerConsumer(msg::SCRIPTS_ACTUATOR);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("primary", false);

        auto check = waitForMatching(bus, msg::AGENT_MANAGER_HEALTH, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "primary" &&
                   item.payload.value("cmd", std::string{}) == "check";
        });
        bus.publish({
            .request_id = check.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", check.request_id},
                {"agent_name", "primary"},
                {"cmd", "check"},
                {"success", true},
                {"message", "ok"},
            },
        });

        auto init = waitForType(bus, msg::AGENT_MANAGER, msg::INIT_DEVICE);
        assert(init.payload.value("agent_name", "") == "primary");
        bus.publish({
            .request_id = init.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", init.request_id},
                {"agent_name", "primary"},
                {"cmd", "init_device"},
                {"success", true},
                {"message", "ready"},
            },
        });

        bool saw_healthy = false;
        const auto healthy_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < healthy_deadline && !saw_healthy) {
            auto item = bus.waitFor(msg::UI, 100);
            saw_healthy = item && item->type == msg::WATCHDOG_STATE &&
                          item->payload.value("state", "") == "HEALTHY" &&
                          item->payload.value("device_started", false) == false;
        }
        assert(saw_healthy);

        watchdog.setMonitoredAgents({"primary", "remote"}, true);

        auto primary_script_check = waitForMatching(bus, msg::AGENT_MANAGER_HEALTH, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "primary" &&
                   item.payload.value("cmd", std::string{}) == "check";
        });
        bus.publish({
            .request_id = primary_script_check.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", primary_script_check.request_id},
                {"agent_name", "primary"},
                {"cmd", "check"},
                {"success", true},
                {"message", "ok"},
            },
        });

        auto remote_script_check = waitForMatching(bus, msg::AGENT_MANAGER_HEALTH, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "remote" &&
                   item.payload.value("cmd", std::string{}) == "check";
        });
        bus.publish({
            .request_id = remote_script_check.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", remote_script_check.request_id},
                {"agent_name", "remote"},
                {"cmd", "check"},
                {"success", false},
                {"message", "missing"},
            },
        });

        auto stop_script = waitForType(bus, msg::SCRIPTS_ACTUATOR, msg::STOP_SCRIPT);
        assert(stop_script.payload.value("reason", std::string{}).find("remote") != std::string::npos);

        bool saw_primary_stop_record = false;
        bool saw_remote_stop_record = false;
        bool saw_disconnected = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline
               && !(saw_primary_stop_record && saw_remote_stop_record && saw_disconnected)) {
            if (auto item = bus.waitFor(msg::AGENT_MANAGER_PRIORITY, 100)) {
                if (item->type == msg::CMD_REQUEST
                    && item->payload.value("cmd", std::string{}) == "stop_record") {
                    const auto agent = item->payload.value("agent_name", std::string{});
                    saw_primary_stop_record = saw_primary_stop_record || agent == "primary";
                    saw_remote_stop_record = saw_remote_stop_record || agent == "remote";
                }
            }
            if (auto item = bus.waitFor(msg::UI, 10)) {
                if (item->type == msg::WATCHDOG_STATE) {
                    saw_disconnected = item->payload.value("state", "") == "DISCONNECTED";
                }
            }
        }
        assert(saw_primary_stop_record);
        assert(saw_remote_stop_record);
        assert(saw_disconnected);

        watchdog.stop();
    }

    {
        HostMessageBus bus;
        bus.registerConsumer(msg::AGENT_MANAGER);
        bus.registerConsumer(msg::AGENT_MANAGER_HEALTH);
        bus.registerConsumer(msg::AGENT_MANAGER_PRIORITY);
        bus.registerConsumer(msg::SCRIPTS_ACTUATOR);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("primary", false);

        auto check = waitForMatching(bus, msg::AGENT_MANAGER_HEALTH, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "primary" &&
                   item.payload.value("cmd", std::string{}) == "check";
        });
        bus.publish({
            .request_id = check.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", check.request_id},
                {"agent_name", "primary"},
                {"cmd", "check"},
                {"success", true},
                {"message", "ok"},
            },
        });

        auto init = waitForType(bus, msg::AGENT_MANAGER, msg::INIT_DEVICE);
        assert(init.payload.value("agent_name", "") == "primary");
        bus.publish({
            .request_id = init.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", init.request_id},
                {"agent_name", "primary"},
                {"cmd", "init_device"},
                {"success", true},
                {"message", "ready"},
            },
        });

        bool saw_healthy = false;
        const auto healthy_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < healthy_deadline && !saw_healthy) {
            auto item = bus.waitFor(msg::UI, 100);
            saw_healthy = item && item->type == msg::WATCHDOG_STATE &&
                          item->payload.value("state", "") == "HEALTHY" &&
                          item->payload.value("device_started", false) == false;
        }
        assert(saw_healthy);

        watchdog.setMonitoredAgents({"primary"}, true);

        auto failed_primary_check = waitForMatching(bus, msg::AGENT_MANAGER_HEALTH, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "primary" &&
                   item.payload.value("cmd", std::string{}) == "check";
        });
        bus.publish({
            .request_id = failed_primary_check.request_id,
            .source = msg::AGENT_MANAGER,
            .target = msg::WATCHDOG,
            .type = msg::CMD_RESULT,
            .payload = {
                {"request_id", failed_primary_check.request_id},
                {"agent_name", "primary"},
                {"cmd", "check"},
                {"success", false},
                {"message", "device missing"},
            },
        });

        auto stop_script = waitForType(bus, msg::SCRIPTS_ACTUATOR, msg::STOP_SCRIPT);
        assert(stop_script.payload.value("reason", std::string{}).find("primary") != std::string::npos);

        bool saw_primary_stop_record = false;
        bool saw_disconnected = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline
               && !(saw_primary_stop_record && saw_disconnected)) {
            if (auto item = bus.waitFor(msg::AGENT_MANAGER_PRIORITY, 100)) {
                if (item->type == msg::CMD_REQUEST
                    && item->payload.value("cmd", std::string{}) == "stop_record"
                    && item->payload.value("agent_name", std::string{}) == "primary") {
                    saw_primary_stop_record = true;
                }
            }
            if (auto item = bus.waitFor(msg::UI, 10)) {
                if (item->type == msg::WATCHDOG_STATE) {
                    saw_disconnected = item->payload.value("state", "") == "DISCONNECTED";
                }
            }
        }
        assert(saw_primary_stop_record);
        assert(saw_disconnected);

        watchdog.stop();
    }

    {
        HostMessageBus bus;
        bus.registerConsumer(msg::AGENT_MANAGER);
        bus.registerConsumer(msg::AGENT_MANAGER_HEALTH);
        bus.registerConsumer(msg::AGENT_MANAGER_PRIORITY);
        bus.registerConsumer(msg::UI);
        Watchdog watchdog(bus);
        watchdog.start();
        watchdog.setActiveAgent("agent", false);

        auto check = waitForType(bus, msg::AGENT_MANAGER_HEALTH, msg::CMD_REQUEST);
        assert(check.payload.value("cmd", "") == "check");
        publishResult(bus, "check", true);

        auto init = waitForType(bus, msg::AGENT_MANAGER, msg::INIT_DEVICE);
        assert(init.payload.value("agent_name", "") == "agent");
        publishResult(bus, "init_device", true);

        bool saw_healthy = false;
        const auto healthy_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < healthy_deadline && !saw_healthy) {
            auto item = bus.waitFor(msg::UI, 100);
            saw_healthy = item && item->type == msg::WATCHDOG_STATE &&
                          item->payload.value("state", "") == "HEALTHY";
        }
        assert(saw_healthy);

        bus.publish({
            .source = msg::SCRIPTS_ACTUATOR,
            .target = msg::WATCHDOG_CONTROL,
            .type = msg::WATCHDOG_ENSURE_DEVICE,
            .payload = {
                {"request_id", "wd-script-1"},
                {"agent_name", "agent"},
                {"source", "script"},
            },
        });

        auto start = waitForMatching(bus, msg::AGENT_MANAGER_PRIORITY, [](const HostMessage& item) {
            return item.type == msg::CMD_REQUEST &&
                   item.payload.value("agent_name", std::string{}) == "agent" &&
                   item.payload.value("cmd", std::string{}) == "start_device";
        });
        assert(start.payload.value("silent", false) == true);
        publishResult(bus, "start_device", true);
        watchdog.stop();
    }

    std::cout << "watchdog lifecycle ok\n";
    return 0;
}
