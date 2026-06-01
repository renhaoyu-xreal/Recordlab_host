#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <string>

#include <action.h>
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
    std::unique_ptr<echo::ActionClient> client_;
    std::string host_;
    int goal_port_;
    int feedback_port_;
    int timeout_ms_;
};

}  // namespace recordlab::host
