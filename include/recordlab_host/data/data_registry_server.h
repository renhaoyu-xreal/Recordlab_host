#pragma once

#include "recordlab_host/bus/host_message_bus.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace recordlab::host {

struct DataStreamRegistration {
    std::string data_name;
    std::string data_type = "topic";
    std::string host = "127.0.0.1";
    int port = 0;
    std::string node_name;
    std::string encoding = "json";
    std::string parse_mode = "json";
    double ui_max_hz = 30.0;
    nlohmann::json qos = nlohmann::json::object();
    nlohmann::json metadata = nlohmann::json::object();
};

class DataRegistryServer {
public:
    DataRegistryServer(HostMessageBus& bus, std::string host, int port);
    ~DataRegistryServer();

    DataRegistryServer(const DataRegistryServer&) = delete;
    DataRegistryServer& operator=(const DataRegistryServer&) = delete;

    void start();
    void stop();
    void registerStatic(const DataStreamRegistration& registration);
    std::vector<DataStreamRegistration> list() const;

private:
    void loop();
    nlohmann::json handleRequest(const nlohmann::json& request);
    std::string keyFor(const DataStreamRegistration& registration) const;
    void publishRegistryEvent(const std::string& type, const DataStreamRegistration& registration);

    HostMessageBus& bus_;
    std::string host_;
    int port_ = 0;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, DataStreamRegistration> streams_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

nlohmann::json toJson(const DataStreamRegistration& registration);
DataStreamRegistration dataStreamRegistrationFromJson(const nlohmann::json& value);

}  // namespace recordlab::host
