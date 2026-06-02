#include "recordlab_host/lifecycle/watchdog.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <algorithm>
#include <sstream>

namespace recordlab::host {
namespace {

std::optional<HostMessage> waitForCommandResult(HostMessageBus& bus,
                                                const std::string& cmd,
                                                int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::nullopt;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        auto opt = bus.waitFor(msg::WATCHDOG, static_cast<int>(std::max<long long>(1, remaining)));
        if (!opt) {
            return std::nullopt;
        }
        if (opt->type == msg::CMD_RESULT && opt->payload.value("cmd", std::string{}) == cmd) {
            return opt;
        }
    }
}

}  // namespace

Watchdog::Watchdog(HostMessageBus& bus) : bus_(bus) {
    bus_.registerConsumer(msg::WATCHDOG);
}

Watchdog::Watchdog(HostMessageBus& bus, std::string agent_name) : Watchdog(bus) {
    setActiveAgent(std::move(agent_name));
}

Watchdog::~Watchdog() {
    stop();
}

void Watchdog::start() {
    if (running_) return;
    running_ = true;
    estop_requested_ = false;
    state_ = AgentHealthState::DISCONNECTED;
    consecutive_failures_ = 0;
    init_failures_ = 0;
    worker_ = std::thread(&Watchdog::workerLoop, this);
}

void Watchdog::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

AgentHealthState Watchdog::state() const {
    return state_.load();
}

void Watchdog::setActiveAgent(std::string agent_name) {
    {
        std::lock_guard<std::mutex> lock(agent_mutex_);
        agent_name_ = std::move(agent_name);
    }
    consecutive_failures_ = 0;
    init_failures_ = 0;
    state_ = AgentHealthState::INITIALIZING;
}

void Watchdog::clearActiveAgent() {
    {
        std::lock_guard<std::mutex> lock(agent_mutex_);
        agent_name_.clear();
    }
    consecutive_failures_ = 0;
    init_failures_ = 0;
    state_ = AgentHealthState::DISCONNECTED;
}

void Watchdog::estop() {
    estop_requested_ = true;
}

// ── Worker thread ──────────────────────────────────────────────

void Watchdog::workerLoop() {
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "started");

    while (running_) {
        if (!hasActiveAgent()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (estop_requested_.exchange(false)) {
            const auto agent_name = activeAgent();
            common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
                "estop triggered for agent=" + agent_name);
            state_ = AgentHealthState::DISCONNECTED;
            consecutive_failures_ = 0;
            init_failures_ = 0;
            publishState(AgentHealthState::DISCONNECTED);
            bus_.publish({
                .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::ESTOP,
                .payload = {{"agent_name", agent_name}},
            });
            continue;
        }

        AgentHealthState current = state_.load();
        AgentHealthState next = current;
        publishState(current);

        switch (current) {
        case AgentHealthState::DISCONNECTED:
            next = doCheck();
            break;
        case AgentHealthState::INITIALIZING:
            next = doInitDevice();
            break;
        case AgentHealthState::ERROR:
            next = doCheck();
            break;
        case AgentHealthState::HEALTHY:
            next = doCheck();
            break;
        }

        if (next != current) {
            const auto old_s = ::recordlab::host::to_string(current);
            const auto new_s = ::recordlab::host::to_string(next);
            common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
                "state transition: " + old_s + " → " + new_s + " agent=" + activeAgent());
        }
        state_ = next;

        publishState(next);

        int interval = kCheckIntervalDisconnectedMs;
        if (next == AgentHealthState::HEALTHY) {
            interval = kCheckIntervalHealthyMs;
        } else if (next == AgentHealthState::INITIALIZING) {
            interval = 100;
        }
        for (int i = 0; i < interval && running_; i += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (estop_requested_) break;
        }
    }

    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "stopped");
}

AgentHealthState Watchdog::doCheck() {
    const auto agent_name = activeAgent();
    common::Logger::instance().log(common::LogLevel::Debug, "Watchdog",
        "sending check for agent=" + agent_name);

    bus_.publish({
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::CMD_REQUEST,
        .payload = {{"cmd", "check"}, {"params", nlohmann::json::object()}},
    });

    auto opt = waitForCommandResult(bus_, "check", 3000);
    if (!opt) {
        consecutive_failures_++;
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check failed for agent=" + agent_name +
            " (consecutive=" + std::to_string(consecutive_failures_) + "/"
            + std::to_string(kMaxCheckFailures) + ")");
        if (consecutive_failures_ >= kMaxCheckFailures) {
            return AgentHealthState::DISCONNECTED;
        }
        return state_.load();
    }

    const bool success = opt->payload.value("success", false);
    if (!success) {
        consecutive_failures_++;
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check returned failure for agent=" + agent_name +
            " (consecutive=" + std::to_string(consecutive_failures_) + ")");
        if (consecutive_failures_ >= kMaxCheckFailures) {
            return AgentHealthState::DISCONNECTED;
        }
        return state_.load();
    }

    consecutive_failures_ = 0;
    const auto current = state_.load();
    if (current == AgentHealthState::DISCONNECTED) {
        init_failures_ = 0;
        return AgentHealthState::INITIALIZING;
    }
    if (current == AgentHealthState::HEALTHY) {
        return AgentHealthState::HEALTHY;
    }
    return current;
}

AgentHealthState Watchdog::doInitDevice() {
    const auto agent_name = activeAgent();
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "triggering init_device for agent=" + agent_name);

    bus_.publish({
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::INIT_DEVICE,
        .payload = {{"agent_name", agent_name}},
    });

    auto opt = waitForCommandResult(bus_, "init_device", 30000);
    if (!opt) {
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device timeout/failure for agent=" + agent_name);
        const int failures = ++init_failures_;
        if (failures <= kMaxInitRetries) {
            doRecoveryClose();
            return AgentHealthState::INITIALIZING;
        }
        publishState(AgentHealthState::ERROR, {
            {"message", "初始化异常，请拔掉眼镜重插后重试"},
            {"init_failures", failures},
        });
        return AgentHealthState::ERROR;
    }

    const bool success = opt->payload.value("success", false);
    if (!success) {
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device failed for agent=" + agent_name);
        const int failures = ++init_failures_;
        if (failures <= kMaxInitRetries) {
            doRecoveryClose();
            return AgentHealthState::INITIALIZING;
        }
        publishState(AgentHealthState::ERROR, {
            {"message", "初始化异常，请拔掉眼镜重插后重试"},
            {"init_failures", failures},
        });
        return AgentHealthState::ERROR;
    }

    init_failures_ = 0;
    consecutive_failures_ = 0;
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "init_device succeeded for agent=" + agent_name);
    return AgentHealthState::HEALTHY;
}

bool Watchdog::doRecoveryClose() {
    const auto agent_name = activeAgent();
    common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
        "attempting device close before init retry for agent=" + agent_name +
        " (init_failures=" + std::to_string(init_failures_.load()) + "/"
        + std::to_string(kMaxInitRetries) + ")");

    bus_.publish({
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::CMD_REQUEST,
        .payload = {{"cmd", "release_device"}, {"params", nlohmann::json::object()}},
    });

    auto opt = waitForCommandResult(bus_, "release_device", 10000);
    const bool success = opt && opt->payload.value("success", false);
    common::Logger::instance().log(
        success ? common::LogLevel::Info : common::LogLevel::Warn,
        "Watchdog",
        "device close before init retry " + std::string(success ? "succeeded" : "failed") +
            " for agent=" + agent_name);
    return success;
}

std::string Watchdog::activeAgent() const {
    std::lock_guard<std::mutex> lock(agent_mutex_);
    return agent_name_;
}

bool Watchdog::hasActiveAgent() const {
    return !activeAgent().empty();
}

void Watchdog::publishState(AgentHealthState state, const nlohmann::json& extra) {
    nlohmann::json payload = {
        {"state", ::recordlab::host::to_string(state)},
        {"agent_name", activeAgent()},
    };
    for (const auto& item : extra.items()) {
        payload[item.key()] = item.value();
    }
    bus_.publish({
        .source = msg::WATCHDOG, .target = msg::UI, .type = msg::WATCHDOG_STATE,
        .payload = payload,
    });
}

}  // namespace recordlab::host
