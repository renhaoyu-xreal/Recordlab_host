#include "recordlab_host/lifecycle/watchdog.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <algorithm>
#include <chrono>

namespace recordlab::host {
namespace {

std::string makeWatchdogRequestId(const std::string& agent_name, const std::string& cmd) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "wd_" + agent_name + "_" + cmd + "_" + std::to_string(ms);
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
    consecutive_failures_ = 0;
    init_failures_ = 0;
    last_reason_ = "startup";
    if (hasActiveAgent()) {
        state_ = AgentHealthState::INITIALIZING;
    } else {
        state_ = AgentHealthState::DISCONNECTED;
    }
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
    last_reason_ = "agent_activated";
    state_ = AgentHealthState::INITIALIZING;
}

void Watchdog::clearActiveAgent() {
    {
        std::lock_guard<std::mutex> lock(agent_mutex_);
        agent_name_.clear();
    }
    consecutive_failures_ = 0;
    init_failures_ = 0;
    last_reason_ = "agent_cleared";
    state_ = AgentHealthState::DISCONNECTED;
}

void Watchdog::estop() {
    estop_requested_ = true;
}

void Watchdog::workerLoop() {
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog", "started");

    while (running_) {
        if (!hasActiveAgent()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (estop_requested_.exchange(false)) {
            const auto agent_name = activeAgent();
            common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
                "estop triggered for agent=" + agent_name,
                {{"agent_name", agent_name}, {"state", "DISCONNECTED"}});
            state_ = AgentHealthState::DISCONNECTED;
            consecutive_failures_ = 0;
            init_failures_ = 0;
            last_reason_ = "estop";
            publishState(AgentHealthState::DISCONNECTED);
            bus_.publish({
                .source = msg::WATCHDOG,
                .target = msg::AGENT_MANAGER,
                .type = msg::ESTOP,
                .payload = {{"agent_name", agent_name}},
            });
            continue;
        }

        const AgentHealthState current = state_.load();
        AgentHealthState next = current;
        publishState(current);

        switch (current) {
        case AgentHealthState::DISCONNECTED:
            next = doCheck();
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
                "state transition: " + old_s + " -> " + new_s + " agent=" + activeAgent(),
                {{"agent_name", activeAgent()}, {"from_state", old_s}, {"state", new_s}});
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

    common::Logger::instance().log(common::LogLevel::Info, "Watchdog", "stopped");
}

AgentHealthState Watchdog::doCheck() {
    const auto agent_name = activeAgent();
    const auto request_id = makeWatchdogRequestId(agent_name, "check");
    common::Logger::instance().log(common::LogLevel::Debug, "Watchdog",
        "sending check for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "check"}});

    bus_.publish({
        .request_id = request_id,
        .source = msg::WATCHDOG,
        .target = msg::AGENT_MANAGER,
        .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id},
            {"agent_name", agent_name},
            {"cmd", "check"},
            {"params", nlohmann::json::object()},
            {"priority", "high"},
            {"silent", true},
        },
    });

    auto opt = waitForResult(request_id, "check", 3000);
    if (!opt) {
        consecutive_failures_++;
        last_reason_ = "check_timeout";
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check timeout for agent=" + agent_name,
            {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "check"}});
        return AgentHealthState::DISCONNECTED;
    }

    if (!opt->payload.value("success", false)) {
        consecutive_failures_++;
        last_reason_ = "check_failed";
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check returned failure for agent=" + agent_name +
                " (consecutive=" + std::to_string(consecutive_failures_.load()) + ")",
            {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "check"}});
        return consecutive_failures_ >= kMaxCheckFailures
            ? AgentHealthState::DISCONNECTED
            : state_.load();
    }

    consecutive_failures_ = 0;
    if (state_.load() == AgentHealthState::DISCONNECTED) {
        init_failures_ = 0;
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
    const auto agent_name = activeAgent();
    const auto request_id = makeWatchdogRequestId(agent_name, "init_device");
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "triggering init_device for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "init_device"}});

    bus_.publish({
        .request_id = request_id,
        .source = msg::WATCHDOG,
        .target = msg::AGENT_MANAGER,
        .type = msg::INIT_DEVICE,
        .payload = {{"request_id", request_id}, {"agent_name", agent_name}},
    });

    auto opt = waitForResult(request_id, "init_device", 30000);
    if (!opt || !opt->payload.value("success", false)) {
        const int failures = ++init_failures_;
        last_reason_ = opt ? "init_device_failed" : "init_device_timeout";
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "init_device failed for agent=" + agent_name +
                " (init_failures=" + std::to_string(failures) + "/" +
                std::to_string(kMaxInitRetries) + ")",
            {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "init_device"}});
        if (failures <= kMaxInitRetries) {
            doRecoveryClose();
            last_reason_ = "init_device_retry";
            return AgentHealthState::INITIALIZING;
        }
        return AgentHealthState::ERROR;
    }

    init_failures_ = 0;
    consecutive_failures_ = 0;
    last_reason_ = "init_device_succeeded";
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "init_device succeeded for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "init_device"}});
    return AgentHealthState::HEALTHY;
}

bool Watchdog::doRecoveryClose() {
    const auto agent_name = activeAgent();
    const auto request_id = makeWatchdogRequestId(agent_name, "release_device");
    common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
        "attempting device close before init retry for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "release_device"}});

    bus_.publish({
        .request_id = request_id,
        .source = msg::WATCHDOG,
        .target = msg::AGENT_MANAGER,
        .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id},
            {"agent_name", agent_name},
            {"cmd", "release_device"},
            {"params", nlohmann::json::object()},
            {"priority", "high"},
            {"silent", true},
        },
    });

    auto opt = waitForResult(request_id, "release_device", 10000);
    const bool success = opt && opt->payload.value("success", false);
    common::Logger::instance().log(
        success ? common::LogLevel::Info : common::LogLevel::Warn,
        "Watchdog",
        "device close before init retry " + std::string(success ? "succeeded" : "failed") +
            " for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "release_device"}});
    return success;
}

std::optional<HostMessage> Watchdog::waitForResult(const std::string& request_id,
                                                   const std::string& cmd,
                                                   int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (running_ && std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        auto opt = bus_.waitFor(msg::WATCHDOG, static_cast<int>(std::min<long long>(remaining, 100)));
        if (!opt || opt->type != msg::CMD_RESULT) continue;

        const std::string payload_request_id = opt->payload.value("request_id", std::string{});
        const std::string payload_cmd = opt->payload.value("cmd", std::string{});
        const bool request_matches = opt->request_id == request_id || payload_request_id == request_id;
        if (request_matches || (!request_id.empty() && opt->request_id.empty() && payload_request_id.empty() && payload_cmd == cmd)) {
            return opt;
        }
    }
    return std::nullopt;
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
        {"agent_name", activeAgent()},
        {"state", ::recordlab::host::to_string(state)},
        {"reason", last_reason_},
        {"consecutive_failures", consecutive_failures_.load()},
        {"init_failures", init_failures_.load()},
    };
    for (const auto& item : extra.items()) {
        payload[item.key()] = item.value();
    }
    bus_.publish({
        .source = msg::WATCHDOG,
        .target = msg::UI,
        .type = msg::WATCHDOG_STATE,
        .payload = std::move(payload),
    });
}

}  // namespace recordlab::host
