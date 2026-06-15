#include "recordlab_host/lifecycle/watchdog.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

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
    start_pending_ = false;
    last_reason_ = "startup";
    state_ = AgentHealthState::DISCONNECTED;
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
        monitored_agent_names_.clear();
        if (!agent_name_.empty()) monitored_agent_names_.push_back(agent_name_);
    }
    consecutive_failures_ = 0;
    init_failures_ = 0;
    failure_stop_sent_ = false;
    script_monitoring_ = false;
    start_pending_ = false;
    last_reason_ = "agent_activated";
    state_ = AgentHealthState::DISCONNECTED;
}

void Watchdog::setMonitoredAgents(std::vector<std::string> agent_names, bool script_monitoring) {
    std::vector<std::string> unique_names;
    std::unordered_set<std::string> seen;
    for (auto& name : agent_names) {
        if (name.empty() || seen.count(name) > 0) continue;
        seen.insert(name);
        unique_names.push_back(std::move(name));
    }
    {
        std::lock_guard<std::mutex> lock(agent_mutex_);
        if (unique_names.empty() && !agent_name_.empty()) {
            unique_names.push_back(agent_name_);
        }
        monitored_agent_names_ = std::move(unique_names);
    }
    consecutive_failures_ = 0;
    failure_stop_sent_ = false;
    script_monitoring_ = script_monitoring;
    last_reason_ = "monitored_agents_updated";
}

void Watchdog::clearActiveAgent() {
    {
        std::lock_guard<std::mutex> lock(agent_mutex_);
        agent_name_.clear();
        monitored_agent_names_.clear();
    }
    consecutive_failures_ = 0;
    init_failures_ = 0;
    failure_stop_sent_ = false;
    script_monitoring_ = false;
    start_pending_ = false;
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
            start_pending_ = false;
            last_reason_ = "estop";
            publishState(AgentHealthState::DISCONNECTED);
            sendEstopToMonitoredAgents();
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
            if (start_pending_.exchange(false)) {
                next = doStartDevice();
            } else {
                next = doCheck();
            }
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
        if (next != current && next == AgentHealthState::ERROR) {
            publishErrorNotification();
        }

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
    const auto monitored_agents = monitoredAgents();
    if (agent_name.empty() && monitored_agents.empty()) {
        last_reason_ = "no_active_agent";
        return AgentHealthState::DISCONNECTED;
    }
    const auto primary_agent = agent_name.empty() ? monitored_agents.front() : agent_name;
    std::string failure_reason;
    if (!checkAgent(primary_agent, 3000, &failure_reason)) {
        consecutive_failures_++;
        last_reason_ = failure_reason;
        if (script_monitoring_) stopScriptAndStopRecords(failure_reason);
        return AgentHealthState::DISCONNECTED;
    }

    for (const auto& monitored_agent : monitored_agents) {
        if (monitored_agent == primary_agent) continue;
        std::string monitored_failure;
        if (!checkAgent(monitored_agent, 3000, &monitored_failure)) {
            consecutive_failures_++;
            last_reason_ = monitored_failure;
            stopScriptAndStopRecords(monitored_failure);
            return AgentHealthState::DISCONNECTED;
        }
    }

    consecutive_failures_ = 0;
    failure_stop_sent_ = false;
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

bool Watchdog::checkAgent(const std::string& agent_name, int timeout_ms, std::string* failure_reason) {
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

    auto opt = waitForResult(request_id, "check", timeout_ms);
    if (!opt) {
        if (failure_reason) *failure_reason = "check_timeout:" + agent_name;
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check timeout for agent=" + agent_name,
            {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "check"}});
        return false;
    }

    if (!opt->payload.value("success", false)) {
        if (failure_reason) *failure_reason = "check_failed:" + agent_name;
        common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
            "check returned failure for agent=" + agent_name +
                " (consecutive=" + std::to_string(consecutive_failures_.load()) + ")",
            {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "check"}});
        return false;
    }
    return true;
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
            doRecoveryReboot();
            last_reason_ = "init_device_retry";
            return AgentHealthState::INITIALIZING;
        }
        return AgentHealthState::ERROR;
    }

    init_failures_ = 0;
    consecutive_failures_ = 0;
    start_pending_ = true;
    last_reason_ = "init_device_succeeded";
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "init_device succeeded for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "init_device"}});
    return AgentHealthState::HEALTHY;
}

AgentHealthState Watchdog::doStartDevice() {
    const auto agent_name = activeAgent();
    const auto request_id = makeWatchdogRequestId(agent_name, "start_device");
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "triggering start_device for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "start_device"}});
    publishUiLog("发送命令: start_device {}", "info", "command");

    bus_.publish({
        .request_id = request_id,
        .source = msg::WATCHDOG,
        .target = msg::AGENT_MANAGER,
        .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id},
            {"agent_name", agent_name},
            {"cmd", "start_device"},
            {"params", nlohmann::json::object()},
            {"priority", "high"},
            {"silent", true},
        },
    });

    auto opt = waitForResult(request_id, "start_device", 30000);
    if (!opt || !opt->payload.value("success", false)) {
        last_reason_ = opt ? "start_device_failed" : "start_device_timeout";
        publishUiLog(
            opt ? "start_device 失败: " + opt->payload.value("message", std::string{})
                : "start_device 超时",
            "error",
            "command");
        common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
            "start_device failed for agent=" + agent_name,
            {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "start_device"}});
        return AgentHealthState::ERROR;
    }

    consecutive_failures_ = 0;
    last_reason_ = "start_device_succeeded";
    publishUiLog("start_device: " + opt->payload.value("message", std::string{}), "success", "command");
    common::Logger::instance().log(common::LogLevel::Info, "Watchdog",
        "start_device succeeded for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "start_device"}});
    return AgentHealthState::HEALTHY;
}

bool Watchdog::doRecoveryReboot() {
    const auto agent_name = activeAgent();
    const auto request_id = makeWatchdogRequestId(agent_name, "reboot_device");
    common::Logger::instance().log(common::LogLevel::Warn, "Watchdog",
        "attempting device reboot before init retry for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "reboot_device"}});

    bus_.publish({
        .request_id = request_id,
        .source = msg::WATCHDOG,
        .target = msg::AGENT_MANAGER,
        .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id},
            {"agent_name", agent_name},
            {"cmd", "reboot_device"},
            {"params", nlohmann::json::object()},
            {"priority", "high"},
            {"silent", true},
        },
    });

    auto opt = waitForResult(request_id, "reboot_device", 60000);
    const bool success = opt && opt->payload.value("success", false);
    common::Logger::instance().log(
        success ? common::LogLevel::Info : common::LogLevel::Warn,
        "Watchdog",
        "device reboot before init retry " + std::string(success ? "succeeded" : "failed") +
            " for agent=" + agent_name,
        {{"request_id", request_id}, {"agent_name", agent_name}, {"cmd", "reboot_device"}});
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

std::vector<std::string> Watchdog::monitoredAgents() const {
    std::lock_guard<std::mutex> lock(agent_mutex_);
    return monitored_agent_names_;
}

bool Watchdog::hasActiveAgent() const {
    return !monitoredAgents().empty();
}

void Watchdog::publishUiLog(const std::string& message,
                            const std::string& level,
                            const std::string& log_type) {
    bus_.publish({
        .source = msg::WATCHDOG,
        .target = msg::UI,
        .type = msg::LOG_ENTRY,
        .payload = {
            {"message", message},
            {"level", level},
            {"log_type", log_type},
        },
    });
}

void Watchdog::publishState(AgentHealthState state, const nlohmann::json& extra) {
    nlohmann::json monitored = nlohmann::json::array();
    for (const auto& name : monitoredAgents()) {
        monitored.push_back(name);
    }
    nlohmann::json payload = {
        {"agent_name", activeAgent()},
        {"monitored_agents", monitored},
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

void Watchdog::sendEstopToMonitoredAgents() {
    auto agents = monitoredAgents();
    const auto active = activeAgent();
    if (!active.empty() && std::find(agents.begin(), agents.end(), active) == agents.end()) {
        agents.push_back(active);
    }
    for (const auto& agent_name : agents) {
        bus_.publish({
            .source = msg::WATCHDOG,
            .target = msg::AGENT_MANAGER,
            .type = msg::ESTOP,
            .payload = {{"agent_name", agent_name}},
        });
    }
}

void Watchdog::sendStopRecordToMonitoredAgents() {
    auto agents = monitoredAgents();
    const auto active = activeAgent();
    if (!active.empty() && std::find(agents.begin(), agents.end(), active) == agents.end()) {
        agents.push_back(active);
    }
    for (const auto& agent_name : agents) {
        const auto request_id = makeWatchdogRequestId(agent_name, "stop_record");
        bus_.publish({
            .request_id = request_id,
            .source = msg::WATCHDOG,
            .target = msg::AGENT_MANAGER,
            .type = msg::CMD_REQUEST,
            .payload = {
                {"request_id", request_id},
                {"agent_name", agent_name},
                {"cmd", "stop_record"},
                {"params", nlohmann::json::object()},
                {"priority", "high"},
                {"silent", true},
            },
        });
    }
}

void Watchdog::stopScriptAndStopRecords(const std::string& reason) {
    if (failure_stop_sent_.exchange(true)) return;
    common::Logger::instance().log(common::LogLevel::Error, "Watchdog",
        "monitored agent failed, stopping script and recordings: " + reason,
        {{"reason", reason}});
    bus_.publish({
        .source = msg::WATCHDOG,
        .target = msg::SCRIPTS_ACTUATOR,
        .type = msg::STOP_SCRIPT,
        .payload = {{"reason", reason}},
    });
    sendStopRecordToMonitoredAgents();
}

void Watchdog::publishErrorNotification() {
    bus_.publish({
        .source = msg::WATCHDOG,
        .target = msg::UI,
        .type = msg::USER_NOTIFICATION,
        .payload = {
            {"severity", "critical"},
            {"error_code", "INIT_DEVICE_FAILED"},
            {"params", nlohmann::json::object()},
            {"agent_name", activeAgent()},
            {"state", "ERROR"},
            {"reason", last_reason_},
        },
    });
}

}  // namespace recordlab::host
