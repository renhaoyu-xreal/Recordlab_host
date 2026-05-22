#pragma once

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <zmq.hpp>

namespace recordlab {

using json = nlohmann::json;

class MasterClient {
 public:
  explicit MasterClient(std::string endpoint = "tcp://127.0.0.1:5590", int timeout_ms = 1000);
  ~MasterClient();

  json call(const std::string &op, const json &data = json::object());
  json rawCall(const json &request);
  void resetSocket();

  json registerNode(const json &data) { return call("register_node", data); }
  json heartbeat(const std::string &node) { return call("heartbeat", {{"node", node}}); }
  json unregisterNode(const std::string &node) { return call("unregister_node", {{"node", node}}); }
  json listNodes() { return call("list_nodes"); }
  json registerPublisher(const json &data) { return call("register_publisher", data); }
  json lookupTopic(const std::string &topic) { return call("lookup_topic", {{"topic", topic}}); }
  json listTopics() { return call("list_topics"); }
  json registerService(const json &data) { return call("register_service", data); }
  json lookupService(const std::string &service) { return call("lookup_service", {{"service", service}}); }
  json listServices() { return call("list_services"); }
  json registerAction(const json &data) { return call("register_action", data); }
  json lookupAction(const std::string &action) { return call("lookup_action", {{"action", action}}); }
  json listActions() { return call("list_actions"); }

 private:
  std::string endpoint_;
  int timeout_ms_;
  zmq::context_t context_{1};
  std::unique_ptr<zmq::socket_t> socket_;
  std::mutex mu_;
};

}  // namespace recordlab
