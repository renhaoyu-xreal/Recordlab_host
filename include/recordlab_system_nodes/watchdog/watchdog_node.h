#pragma once

#include "recordlab_echo/echo.h"
#include "recordlab_core/node_base.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace recordlab::nodes {

class WatchdogNode : public NodeBase {
 public:
  explicit WatchdogNode(std::string endpoint = "tcp://127.0.0.1:5590");
  ~WatchdogNode() override;

  bool start() override;
  void stop() override;
  json evaluateTarget(const json &nodes, const std::string &target) const;
  json evaluateTarget(const json &nodes, const std::string &target, const json &device_state) const;
  std::string target() const;

 private:
  json setTarget(const json &request);
  json clearTarget(const json &request);
  void refreshDeviceStateSubscription(const std::string &target);
  json latestDeviceState() const;
  void loop();
  void publishState(const json &state);

  mutable std::mutex target_mu_;
  std::string target_;
  mutable std::mutex device_state_mu_;
  json latest_device_state_ = json::object();
  std::string subscribed_state_topic_;
  std::string subscribed_state_endpoint_;
  std::unique_ptr<Subscriber> device_state_sub_;
  std::unique_ptr<Publisher> state_pub_;
  std::unique_ptr<ServiceServer> set_target_service_;
  std::unique_ptr<ServiceServer> clear_target_service_;
  std::atomic<bool> monitor_running_{false};
  std::thread monitor_thread_;
};

}  // namespace recordlab::nodes
