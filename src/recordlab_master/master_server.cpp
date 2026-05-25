#include "recordlab_master/master_server.h"

#include <chrono>
#include <iostream>

namespace recordlab {

GraphEventBus::GraphEventBus(int port) {
  pub_.set(zmq::sockopt::linger, 0);
  pub_.bind("tcp://*:" + std::to_string(port));
}

GraphEventBus::~GraphEventBus() {
  pub_.close();
}

void GraphEventBus::publish(const RegistryEvent &event) {
  json payload = {{"event", event.type}, {"data", event.data}, {"timestamp_ms", nowMs()}};
  const std::string text = payload.dump();
  pub_.send(zmq::buffer(text), zmq::send_flags::dontwait);
}

MasterServer::MasterServer(int rpc_port, int graph_port, int64_t lease_timeout_ms)
    : rpc_port_(rpc_port), store_(lease_timeout_ms), graph_(graph_port) {}

MasterServer::~MasterServer() { stop(); }

void MasterServer::start() {
  if (running_.exchange(true)) return;
  rep_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::rep);
  rep_->set(zmq::sockopt::linger, 0);
  rep_->bind("tcp://*:" + std::to_string(rpc_port_));
  server_thread_ = std::thread(&MasterServer::loop, this);
  lease_thread_ = std::thread(&MasterServer::leaseLoop, this);
}

void MasterServer::stop() {
  if (!running_.exchange(false)) return;
  if (rep_) rep_->close();
  context_.close();
  if (server_thread_.joinable()) server_thread_.join();
  if (lease_thread_.joinable()) lease_thread_.join();
}

void MasterServer::loop() {
  while (running_) {
    try {
      zmq::message_t req;
      auto ok = rep_->recv(req, zmq::recv_flags::dontwait);
      if (!ok) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      json request = json::parse(std::string(static_cast<char *>(req.data()), req.size()));
      json response = handleRequest(request);
      const std::string text = response.dump();
      rep_->send(zmq::buffer(text), zmq::send_flags::none);
    } catch (const std::exception &e) {
      try {
        json response = {{"ok", false}, {"error", e.what()}, {"data", nullptr}};
        const std::string text = response.dump();
        rep_->send(zmq::buffer(text), zmq::send_flags::none);
      } catch (...) {
      }
    }
  }
}

void MasterServer::leaseLoop() {
  while (running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    publishEvents(store_.checkLeases());
  }
}

void MasterServer::publishEvents(const std::vector<RegistryEvent> &events) {
  for (const auto &event : events) graph_.publish(event);
}

json MasterServer::handleRequest(const json &request) {
  const std::string op = request.value("op", "");
  const json data = request.value("data", json::object());
  json result;
  std::vector<RegistryEvent> events;

  if (op == "register_node") events = store_.registerNode(data);
  else if (op == "heartbeat") events = store_.heartbeat(data);
  else if (op == "unregister_node") events = store_.unregisterNode(data);
  else if (op == "list_nodes") result = store_.listNodes();
  else if (op == "register_publisher") events = store_.registerPublisher(data);
  else if (op == "unregister_publisher") events = store_.unregisterPublisher(data);
  else if (op == "lookup_topic") result = store_.lookupTopic(data.value("topic", ""));
  else if (op == "list_topics") result = store_.listTopics();
  else if (op == "register_service") events = store_.registerService(data);
  else if (op == "unregister_service") events = store_.unregisterService(data);
  else if (op == "lookup_service") result = store_.lookupService(data.value("service", ""));
  else if (op == "list_services") result = store_.listServices();
  else if (op == "register_action") events = store_.registerAction(data);
  else if (op == "unregister_action") events = store_.unregisterAction(data);
  else if (op == "lookup_action") result = store_.lookupAction(data.value("action", ""));
  else if (op == "list_actions") result = store_.listActions();
  else if (op == "register_type") events = store_.registerType(data);
  else if (op == "lookup_type") result = store_.lookupType(data.value("type_name", ""));
  else if (op == "list_types") result = store_.listTypes();
  else if (op == "set_param") events = store_.setParam(data);
  else if (op == "get_param") result = store_.getParam(data.value("name", ""));
  else if (op == "list_params") result = store_.listParams();
  else if (op == "delete_param") events = store_.deleteParam(data);
  else return {{"ok", false}, {"error", "unknown op: " + op}, {"data", nullptr}};

  publishEvents(events);
  if (!events.empty() && result.is_null()) {
    result = json::array();
    for (const auto &event : events) result.push_back({{"event", event.type}, {"data", event.data}});
  }
  return {{"ok", true}, {"error", ""}, {"data", result.is_null() ? json::object() : result}};
}

}  // namespace recordlab
