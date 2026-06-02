#include "recordlab_host/communication/echo_action_client.h"
#include "recordlab_host/common/logger.h"

#include <stdexcept>

namespace recordlab::host {

EchoActionClient::EchoActionClient(std::string host, int goal_port, int feedback_port, int timeout_ms)
    : host_(std::move(host)), goal_port_(goal_port), feedback_port_(feedback_port), timeout_ms_(timeout_ms) {
    common::Logger::instance().log(
        common::LogLevel::Info,
        "EchoActionClient",
        "create fixed-port client host=" + host_ +
            ", goal_port=" + std::to_string(goal_port_) +
            ", feedback_port=" + std::to_string(feedback_port_) +
            ", timeout_ms=" + std::to_string(timeout_ms_));
    client_ = std::make_unique<echo::ActionClient>(
        "recordlab_python_action", host_, goal_port_, host_, feedback_port_, timeout_ms_);
}

EchoActionClient::~EchoActionClient() = default;

bool EchoActionClient::waitForServer(int timeout_ms) {
    try {
        auto result = sendCommand("check", nlohmann::json::object(), timeout_ms);
        return result.success || result.result.is_object();
    } catch (...) {
        return false;
    }
}

ActionResult EchoActionClient::sendCommand(const std::string& cmd, const nlohmann::json& params, int timeout_ms) {
    common::Logger::instance().log(
        common::LogLevel::Info,
        "EchoActionClient",
        "send command cmd=" + cmd + ", timeout_ms=" + std::to_string(timeout_ms) +
            ", params=" + params.dump());

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    ActionResult result;

    try {
        uint32_t goal_id = client_->sendGoal(
            {{"cmd", cmd}, {"params", params}},
            nullptr,
            [&](uint32_t id, const nlohmann::json& payload, bool success) {
                std::lock_guard<std::mutex> lock(mutex);
                result.goal_id = std::to_string(id);
                result.result = payload;
                result.success = success;
                result.status = success ? "SUCCEEDED" : "FAILED";
                done = true;
                cv.notify_one();
            });
        common::Logger::instance().log(
            common::LogLevel::Info,
            "EchoActionClient",
            "sent command cmd=" + cmd + ", goal_id=" + std::to_string(goal_id));
    } catch (const std::exception& e) {
        common::Logger::instance().log(
            common::LogLevel::Error,
            "EchoActionClient",
            "sendGoal threw exception cmd=" + cmd + ": " + std::string(e.what()));
        return {"", nlohmann::json::object(), std::string("sendGoal failed: ") + e.what(), false};
    }

    std::unique_lock<std::mutex> lock(mutex);
    if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() { return done; })) {
        common::Logger::instance().log(
            common::LogLevel::Error,
            "EchoActionClient",
            "timeout waiting result cmd=" + cmd);
        return {"", nlohmann::json{{"message", "Timed out waiting for action result"}}, "TIMEOUT", false};
    }
    common::Logger::instance().log(
        result.success ? common::LogLevel::Info : common::LogLevel::Warn,
        "EchoActionClient",
        "result cmd=" + cmd +
            ", goal_id=" + result.goal_id +
            ", success=" + (result.success ? std::string("true") : std::string("false")) +
            ", payload=" + result.result.dump());
    return result;
}

}  // namespace recordlab::host
