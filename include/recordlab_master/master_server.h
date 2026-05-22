#pragma once

#include "recordlab_master/registries.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <zmq.hpp>

namespace recordlab {

class GraphEventBus {
 public:
  explicit GraphEventBus(int port = 5591);
  ~GraphEventBus();
  void publish(const RegistryEvent &event);

 private:
  zmq::context_t context_{1};
  zmq::socket_t pub_{context_, zmq::socket_type::pub};
};

class MasterServer {
 public:
  MasterServer(int rpc_port = 5590, int graph_port = 5591, int64_t lease_timeout_ms = 3000);
  ~MasterServer();
  void start();
  void stop();
  bool running() const { return running_; }
  json handleRequest(const json &request);

 private:
  void loop();
  void leaseLoop();
  void publishEvents(const std::vector<RegistryEvent> &events);

  int rpc_port_;
  std::atomic<bool> running_{false};
  RegistryStore store_;
  GraphEventBus graph_;
  zmq::context_t context_{1};
  std::unique_ptr<zmq::socket_t> rep_;
  std::thread server_thread_;
  std::thread lease_thread_;
};

}  // namespace recordlab
