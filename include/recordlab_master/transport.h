#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <zmq.hpp>

namespace recordlab {

using json = nlohmann::json;

int bindRandomTcpPort(zmq::socket_t &socket);

class Publisher {
 public:
  explicit Publisher(std::string topic);
  ~Publisher();
  int port() const { return port_; }
  std::string endpoint() const { return "tcp://127.0.0.1:" + std::to_string(port_); }
  void publish(const json &payload);

 private:
  std::string topic_;
  zmq::context_t context_{1};
  zmq::socket_t socket_{context_, zmq::socket_type::pub};
  int port_{0};
  std::mutex mu_;
};

class Subscriber {
 public:
  using Callback = std::function<void(const json &)>;
  Subscriber(std::string endpoint, std::string topic, Callback cb);
  ~Subscriber();

 private:
  void loop();
  std::string topic_;
  Callback cb_;
  std::atomic<bool> running_{true};
  zmq::context_t context_{1};
  zmq::socket_t socket_{context_, zmq::socket_type::sub};
  std::thread thread_;
};

class ServiceServer {
 public:
  using Callback = std::function<json(const json &)>;
  explicit ServiceServer(Callback cb);
  ~ServiceServer();
  int port() const { return port_; }
  std::string endpoint() const { return "tcp://127.0.0.1:" + std::to_string(port_); }

 private:
  void loop();
  Callback cb_;
  std::atomic<bool> running_{true};
  zmq::context_t context_{1};
  zmq::socket_t socket_{context_, zmq::socket_type::rep};
  int port_{0};
  std::thread thread_;
};

class ServiceClient {
 public:
  explicit ServiceClient(std::string endpoint, int timeout_ms = 1000);
  ~ServiceClient();
  json call(const json &request);
  void resetSocket();

 private:
  std::string endpoint_;
  int timeout_ms_;
  zmq::context_t context_{1};
  std::unique_ptr<zmq::socket_t> socket_;
  std::mutex mu_;
};

class ActionServer {
 public:
  using GoalCallback = std::function<json(const json &, std::function<void(const json &)>, std::atomic<bool> &)>;
  explicit ActionServer(GoalCallback cb);
  ~ActionServer() = default;
  json descriptor() const;

 private:
  GoalCallback cb_;
  ServiceServer goal_;
  ServiceServer cancel_;
  Publisher feedback_;
  Publisher result_;
  std::mutex goals_mu_;
  std::map<uint64_t, std::shared_ptr<std::atomic<bool>>> cancel_flags_;
  std::atomic<uint64_t> next_goal_id_{1};
};

class ActionClient {
 public:
  explicit ActionClient(json descriptor, int timeout_ms = 1000);
  uint64_t sendGoal(const json &goal);
  json waitForResult(uint64_t goal_id, int timeout_ms = 5000);
  json cancel(uint64_t goal_id);

 private:
  json descriptor_;
  ServiceClient goal_;
  ServiceClient cancel_;
  std::mutex results_mu_;
  std::map<uint64_t, json> results_;
  std::unique_ptr<Subscriber> result_sub_;
};

}  // namespace recordlab
