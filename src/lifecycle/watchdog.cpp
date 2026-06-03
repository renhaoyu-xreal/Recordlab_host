#include "recordlab_host/lifecycle/watchdog.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <chrono>
#include <algorithm>
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
    last_reason_ = "startup";
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
            publishState(AgentHealthState::DISCONNECTED, "estop");
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
                common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
                    "state transition: DISCONNECTED → INITIALIZING agent=" + agent_name_);
                state_ = AgentHealthState::INITIALIZING;
                publishState(AgentHealthState::INITIALIZING, last_reason_);
                current = AgentHealthState::INITIALIZING;
                next = doInitDevice();
            }
            break;
        case AgentHealthState::INITIALIZING:
            next = doInitDevice();
            break;
        case AgentHealthState::HEALTHY:
            next = doCheck();
            break;
        case AgentHealthState::ERROR:
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

        publishState(next, last_reason_);

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

    auto opt = waitForResult(request_id, 3000);
    if (!opt) {
        consecutive_failures_++;
        last_reason_ = "check_timeout";
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check failed for agent=" + agent_name_ +
            " (consecutive=" + std::to_string(consecutive_failures_) + "/"
            + std::to_string(kMaxCheckFailures) + ")");
        return AgentHealthState::DISCONNECTED;
    }

    const bool success = opt->payload.value("success", false);
    if (!success) {
        consecutive_failures_++;
        last_reason_ = "check_failed";
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check returned failure for agent=" + agent_name_ +
            " (consecutive=" + std::to_string(consecutive_failures_) + ")");
        return AgentHealthState::DISCONNECTED;
    }

    consecutive_failures_ = 0;
    if (state_.load() == AgentHealthState::DISCONNECTED) {
        last_reason_ = "check_succeeded";
        return AgentHealthState::INITIALIZING;
    }
    if (state_.load() == AgentHealthState::ERROR) {
        last_reason_ = "error_check_still_succeeds";
        return AgentHealthState::ERROR;
    }
    last_reason_ = "check_succeeded";
    return AgentHealthState::HEALTHY;
}

AgentHealthState Watchdog::doInitDevice() {
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "triggering init_device for agent=" + agent_name_);

    const auto request_id = makeWatchdogRequestId(agent_name_, "init_device");
    bus_.publish({
        .request_id = request_id,
        .source = msg::WATCHDOG, .target = msg::AGENT_MANAGER, .type = msg::INIT_DEVICE,
        .payload = {{"request_id", request_id}, {"agent_name", agent_name_}},
    });

    auto opt = waitForResult(request_id, 10000);
    if (!opt) {
        last_reason_ = "init_device_timeout";
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device timeout/failure for agent=" + agent_name_);
        return AgentHealthState::ERROR;
    }

    const bool success = opt->payload.value("success", false);
    if (!success) {
        last_reason_ = "init_device_failed";
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device failed for agent=" + agent_name_);
        return AgentHealthState::ERROR;
    }

    consecutive_failures_ = 0;
    last_reason_ = "init_device_succeeded";
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "init_device succeeded for agent=" + agent_name_);
    return AgentHealthState::HEALTHY;
}

std::optional<HostMessage> Watchdog::waitForResult(const std::string& request_id, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (running_ && std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        auto opt = bus_.waitFor(msg::WATCHDOG, static_cast<int>(std::min<long long>(remaining, 100)));
        if (!opt) continue;
        const std::string payload_request_id = opt->payload.value("request_id", std::string{});
        if (opt->type == msg::CMD_RESULT &&
            (opt->request_id == request_id || payload_request_id == request_id)) {
            return opt;
        }
    }
    return std::nullopt;
}

void Watchdog::publishState(AgentHealthState state, const std::string& reason) {
    bus_.publish({
        .source = msg::WATCHDOG,
        .target = msg::UI,
        .type = msg::WATCHDOG_STATE,
        .payload = {
            {"agent_name", agent_name_},
            {"state", ::recordlab::host::to_string(state)},
            {"reason", reason},
            {"consecutive_failures", consecutive_failures_.load()},
        },
    });
}

}  // namespace recordlab::host
