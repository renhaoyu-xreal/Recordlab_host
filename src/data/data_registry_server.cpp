#include "recordlab_host/data/data_registry_server.h"

#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <zmq.hpp>

#include <chrono>
#include <utility>

namespace recordlab::host {

nlohmann::json toJson(const DataStreamRegistration& r) {
    return {
        {"data_name", r.data_name},
        {"data_type", r.data_type},
        {"host", r.host},
        {"port", r.port},
        {"node_name", r.node_name},
        {"encoding", r.encoding},
        {"parse_mode", r.parse_mode},
        {"ui_max_hz", r.ui_max_hz},
        {"qos", r.qos},
        {"metadata", r.metadata},
    };
}

DataStreamRegistration dataStreamRegistrationFromJson(const nlohmann::json& value) {
    DataStreamRegistration r;
    r.data_name = value.value("data_name", value.value("name", std::string{}));
    r.data_type = value.value("data_type", value.value("type", std::string("topic")));
    r.host = value.value("host", std::string("127.0.0.1"));
    r.port = value.value("port", 0);
    r.node_name = value.value("node_name", std::string{});
    r.encoding = value.value("encoding", std::string("json"));
    r.parse_mode = value.value("parse_mode", std::string("json"));
    r.ui_max_hz = value.value("ui_max_hz", 30.0);
    r.qos = value.value("qos", nlohmann::json::object());
    r.metadata = value.value("metadata", nlohmann::json::object());
    return r;
}

DataRegistryServer::DataRegistryServer(HostMessageBus& bus, std::string host, int port)
    : bus_(bus), host_(std::move(host)), port_(port) {}

DataRegistryServer::~DataRegistryServer() {
    stop();
}

void DataRegistryServer::start() {
    if (running_) return;
    running_ = true;
    worker_ = std::thread(&DataRegistryServer::loop, this);
}

void DataRegistryServer::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void DataRegistryServer::registerStatic(const DataStreamRegistration& registration) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_[keyFor(registration)] = registration;
    }
    publishRegistryEvent(msg::DATA_REGISTERED, registration);
}

std::vector<DataStreamRegistration> DataRegistryServer::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DataStreamRegistration> result;
    result.reserve(streams_.size());
    for (const auto& [_, stream] : streams_) {
        result.push_back(stream);
    }
    return result;
}

void DataRegistryServer::loop() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    int linger_ms = 0;
    int timeout_ms = 100;
    socket.setsockopt(ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
    socket.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket.bind("tcp://*:" + std::to_string(port_));
    common::Logger::instance().log(common::LogLevel::Info, "DataRegistryServer",
                                   "started on " + host_ + ":" + std::to_string(port_));
    while (running_) {
        try {
            zmq::message_t request_msg;
            auto received = socket.recv(&request_msg);
            if (!received) continue;
            const std::string request_text(static_cast<const char*>(request_msg.data()), request_msg.size());
            nlohmann::json response;
            try {
                response = handleRequest(nlohmann::json::parse(request_text));
            } catch (const std::exception& e) {
                response = {{"success", false}, {"error", e.what()}};
            }
            const std::string response_text = response.dump();
            zmq::message_t response_msg(response_text.data(), response_text.size());
            socket.send(response_msg);
        } catch (const std::exception& e) {
            common::Logger::instance().log(common::LogLevel::Warn, "DataRegistryServer", e.what());
        }
    }
    socket.close();
}

nlohmann::json DataRegistryServer::handleRequest(const nlohmann::json& request) {
    const std::string action = request.value("action", request.value("type", std::string{}));
    if (action == "register_data") {
        const auto registration = dataStreamRegistrationFromJson(request.value("stream", request));
        if (registration.data_name.empty() || registration.port <= 0) {
            return {{"success", false}, {"error", "data_name and port are required"}};
        }
        registerStatic(registration);
        return {{"success", true}, {"stream", toJson(registration)}};
    }
    if (action == "unregister_data") {
        const auto registration = dataStreamRegistrationFromJson(request.value("stream", request));
        DataStreamRegistration removed = registration;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = streams_.find(keyFor(registration));
            if (it != streams_.end()) {
                removed = it->second;
                streams_.erase(it);
                found = true;
            }
        }
        if (found) publishRegistryEvent(msg::DATA_UNREGISTERED, removed);
        return {{"success", true}, {"found", found}};
    }
    if (action == "list_data") {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& stream : list()) arr.push_back(toJson(stream));
        return {{"success", true}, {"streams", arr}};
    }
    return {{"success", false}, {"error", "unknown registry action"}};
}

std::string DataRegistryServer::keyFor(const DataStreamRegistration& registration) const {
    return registration.node_name + "/" + registration.data_name + "@" + std::to_string(registration.port);
}

void DataRegistryServer::publishRegistryEvent(const std::string& type, const DataStreamRegistration& registration) {
    bus_.publish({
        .source = "data_registry",
        .target = msg::DATA_RECEIVER,
        .type = type,
        .payload = {{"stream", toJson(registration)}},
        .coalesce_key = "data_registry:" + keyFor(registration),
    });
    bus_.publish({
        .source = "data_registry",
        .target = msg::UI,
        .type = type,
        .payload = {{"stream", toJson(registration)}},
        .coalesce_key = "data_registry_ui:" + keyFor(registration),
    });
}

}  // namespace recordlab::host
