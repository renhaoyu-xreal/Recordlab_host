#include "recordlab_host/lifecycle/watchdog.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <chrono>
#include <sstream>

namespace recordlab::host {
namespace {

std::string makeWatchdogRequestId(const std::string& agent_name, const std::string& cmd) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "wd_" + agent_name + "_" + cmd + "_" + std::to_string(ms);
}

}  // namespace

Watchdog::Watchdog(HostMessageBus& bus, std::string agent_name)
    : bus_(bus), agent_name_(std::move(agent_name)) {
    bus_.registerConsumer(msg::WATCHDOG);
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

void Watchdog::estop() {
    estop_requested_ = true;
}

// ── Worker thread ──────────────────────────────────────────────

void Watchdog::workerLoop() {
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "started for agent=" + agent_name_);

    while (running_) {
        if (estop_requested_.exchange(false)) {
            common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
                "estop triggered for agent=" + agent_name_);
            state_ = AgentHealthState::DISCONNECTED;
            consecutive_failures_ = 0;
            bus_.publish({
                .source = msg::WATCHDOG, .target = msg::UI, .type = msg::WATCHDOG_STATE,
                .payload = {{"state", ::recordlab::host::to_string(AgentHealthState::DISCONNECTED)}},
            });
            bus_.publish({
                .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::ESTOP,
                .payload = {{"agent_name", agent_name_}},
            });
            continue;
        }

        AgentHealthState current = state_.load();
        AgentHealthState next = current;

        switch (current) {
        case AgentHealthState::DISCONNECTED:
            next = doCheck();
            if (next == AgentHealthState::INITIALIZING) {
                // check succeeded first time; immediately attempt init_device.
                next = doInitDevice();
            }
            break;
        case AgentHealthState::INITIALIZING:
            // Do not check while init_device is in progress.
            // This state is transient and should only be reached if
            // doInitDevice is taking longer than one cycle.
            break;
        case AgentHealthState::HEALTHY:
            next = doCheck();
            break;
        }

        if (next != current) {
            const auto old_s = ::recordlab::host::to_string(current);
            const auto new_s = ::recordlab::host::to_string(next);
            common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
                "state transition: " + old_s + " → " + new_s + " agent=" + agent_name_);
        }
        state_ = next;

        // Notify UI of the new state.
        bus_.publish({
            .source = msg::WATCHDOG, .target = msg::UI, .type = msg::WATCHDOG_STATE,
            .payload = {{"state", ::recordlab::host::to_string(next)}},
        });

        // Wait before next cycle.
        int interval = (next == AgentHealthState::HEALTHY)
            ? kCheckIntervalHealthyMs
            : kCheckIntervalDisconnectedMs;
        for (int i = 0; i < interval && running_; i += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (estop_requested_) break;
        }
    }

    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "stopped for agent=" + agent_name_);
}

AgentHealthState Watchdog::doCheck() {
    common::Logger::instance().log(common::LogLevel::Debug, "Watchdog",
        "sending check for agent=" + agent_name_);

    const auto request_id = makeWatchdogRequestId(agent_name_, "check");
    bus_.publish({
        .request_id = request_id,
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id},
            {"agent_name", agent_name_},
            {"cmd", "check"},
            {"params", nlohmann::json::object()},
            {"priority", "high"},
            {"silent", true},
        },
    });

    auto opt = bus_.waitFor(msg::WATCHDOG, 3000);
    if (!opt || opt->type != msg::CMD_RESULT) {
        consecutive_failures_++;
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check failed for agent=" + agent_name_ +
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
            "check returned failure for agent=" + agent_name_ +
            " (consecutive=" + std::to_string(consecutive_failures_) + ")");
        if (consecutive_failures_ >= kMaxCheckFailures) {
            return AgentHealthState::DISCONNECTED;
        }
        return state_.load();
    }

    consecutive_failures_ = 0;
    if (state_.load() == AgentHealthState::DISCONNECTED) {
        return AgentHealthState::INITIALIZING;
    }
    return AgentHealthState::HEALTHY;
}

AgentHealthState Watchdog::doInitDevice() {
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "triggering init_device for agent=" + agent_name_);

    bus_.publish({
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::INIT_DEVICE,
        .payload = {{"agent_name", agent_name_}},
    });

    auto opt = bus_.waitFor(msg::WATCHDOG, 10000);
    if (!opt || opt->type != msg::CMD_RESULT) {
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device timeout/failure for agent=" + agent_name_);
        return AgentHealthState::DISCONNECTED;
    }

    const bool success = opt->payload.value("success", false);
    if (!success) {
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device failed for agent=" + agent_name_);
        return AgentHealthState::DISCONNECTED;
    }

    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "init_device succeeded for agent=" + agent_name_);
    return AgentHealthState::HEALTHY;
}

}  // namespace recordlab::host
