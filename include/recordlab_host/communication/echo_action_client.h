#pragma once

#include <chrono>
#include <string>

#include <nlohmann/json.hpp>

namespace recordlab::host {

struct ActionResult {
    std::string goal_id;
    nlohmann::json result;
    std::string status;
    bool success = false;
};

class EchoActionClient {
public:
    EchoActionClient(std::string host, int goal_port, int feedback_port, int timeout_ms = 3000);
    ~EchoActionClient();

    EchoActionClient(const EchoActionClient&) = delete;
    EchoActionClient& operator=(const EchoActionClient&) = delete;

    bool waitForServer(int timeout_ms);
    ActionResult sendCommand(const std::string& cmd, const nlohmann::json& params, int timeout_ms = 5000);

private:
    void* context_ = nullptr;
    void* goal_socket_ = nullptr;
    void* feedback_socket_ = nullptr;
    std::string host_;
    int goal_port_;
    int feedback_port_;
    int timeout_ms_;
    unsigned long long next_goal_id_ = 1;

    nlohmann::json callGoalSocket(const nlohmann::json& request);
    ActionResult waitForResult(const std::string& goal_id, int timeout_ms);
};

}  // namespace recordlab::host
