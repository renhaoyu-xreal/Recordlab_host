#include "recordlab_host/communication/echo_action_client.h"

#include <stdexcept>
#include <string>

#include <zmq.h>

namespace recordlab::host {

namespace {

void setTimeouts(void* socket, int timeout_ms) {
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    zmq_setsockopt(socket, ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    int linger = 0;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
}

std::string recvString(void* socket) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    int rc = zmq_msg_recv(&msg, socket, 0);
    if (rc < 0) {
        zmq_msg_close(&msg);
        throw std::runtime_error("ZMQ receive failed");
    }
    std::string data(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
    zmq_msg_close(&msg);
    return data;
}

void sendString(void* socket, const std::string& data) {
    int rc = zmq_send(socket, data.data(), data.size(), 0);
    if (rc < 0) {
        throw std::runtime_error("ZMQ send failed");
    }
}

}  // namespace

EchoActionClient::EchoActionClient(std::string host, int goal_port, int feedback_port, int timeout_ms)
    : host_(std::move(host)), goal_port_(goal_port), feedback_port_(feedback_port), timeout_ms_(timeout_ms) {
    context_ = zmq_ctx_new();
    goal_socket_ = zmq_socket(context_, ZMQ_REQ);
    feedback_socket_ = zmq_socket(context_, ZMQ_SUB);
    setTimeouts(goal_socket_, timeout_ms_);
    setTimeouts(feedback_socket_, timeout_ms_);
    const std::string goal_endpoint = "tcp://" + host_ + ":" + std::to_string(goal_port_);
    const std::string feedback_endpoint = "tcp://" + host_ + ":" + std::to_string(feedback_port_);
    if (zmq_connect(goal_socket_, goal_endpoint.c_str()) != 0) {
        throw std::runtime_error("Failed to connect goal socket: " + goal_endpoint);
    }
    if (zmq_connect(feedback_socket_, feedback_endpoint.c_str()) != 0) {
        throw std::runtime_error("Failed to connect feedback socket: " + feedback_endpoint);
    }
    zmq_setsockopt(feedback_socket_, ZMQ_SUBSCRIBE, "", 0);
}

EchoActionClient::~EchoActionClient() {
    if (goal_socket_) zmq_close(goal_socket_);
    if (feedback_socket_) zmq_close(feedback_socket_);
    if (context_) zmq_ctx_term(context_);
}

bool EchoActionClient::waitForServer(int timeout_ms) {
    try {
        auto response = callGoalSocket({{"goal_id", "probe"}, {"goal", {{"ping", true}}}});
        return response.value("status", "") == "ACCEPTED";
    } catch (...) {
        return false;
    }
}

nlohmann::json EchoActionClient::callGoalSocket(const nlohmann::json& request) {
    sendString(goal_socket_, request.dump());
    return nlohmann::json::parse(recvString(goal_socket_));
}

ActionResult EchoActionClient::sendCommand(const std::string& cmd, const nlohmann::json& params, int timeout_ms) {
    const std::string goal_id = "cpp-" + std::to_string(next_goal_id_++);
    auto response = callGoalSocket({{"goal_id", goal_id}, {"goal", {{"cmd", cmd}, {"params", params}}}});
    if (response.value("status", "") != "ACCEPTED") {
        throw std::runtime_error("Goal rejected");
    }
    return waitForResult(goal_id, timeout_ms);
}

ActionResult EchoActionClient::waitForResult(const std::string& goal_id, int timeout_ms) {
    zmq_pollitem_t items[] = {{feedback_socket_, 0, ZMQ_POLLIN, 0}};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count());
        int rc = zmq_poll(items, 1, remaining);
        if (rc <= 0) {
            continue;
        }
        std::string topic = recvString(feedback_socket_);
        int more = 0;
        size_t more_size = sizeof(more);
        zmq_getsockopt(feedback_socket_, ZMQ_RCVMORE, &more, &more_size);
        std::string payload = more ? recvString(feedback_socket_) : topic;
        auto msg = nlohmann::json::parse(payload);
        if (msg.value("type", "") != "result" || msg.value("goal_id", "") != goal_id) {
            continue;
        }
        ActionResult result;
        result.goal_id = goal_id;
        result.result = msg.value("result", nlohmann::json::object());
        result.status = msg.value("status", "");
        result.success = result.status == "SUCCEEDED";
        return result;
    }
    throw std::runtime_error("Timed out waiting for action result: " + goal_id);
}

}  // namespace recordlab::host
