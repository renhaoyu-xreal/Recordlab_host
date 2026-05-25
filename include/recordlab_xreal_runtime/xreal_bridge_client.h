#pragma once

#include "recordlab_echo/stdio_channel.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace recordlab::xreal_runtime {

struct XrealBridgeCallbacks {
  std::function<void(const json &)> imu_batch;
  std::function<void(const json &, const std::vector<uint8_t> &)> camera;
};

class XrealBridgeClient {
 public:
  explicit XrealBridgeClient(std::string project_root = std::string(RECORDLAB_MASTER_SOURCE_DIR));
  ~XrealBridgeClient();

  XrealBridgeClient(const XrealBridgeClient &) = delete;
  XrealBridgeClient &operator=(const XrealBridgeClient &) = delete;

  void setCallbacks(XrealBridgeCallbacks callbacks);
  json enumerateDevices();
  json createGlasses(int timeout_ms = 30000);
  json openGlasses(int timeout_ms = 30000);
  json configureGlasses(const json &params, int timeout_ms = 5000);
  json startSensors(int sensor_mask, int timeout_ms = 10000);
  json stopSensors(int sensor_mask, int timeout_ms = 5000);
  json getGlassesState(int timeout_ms = 5000);
  json closeGlasses(int timeout_ms = 5000);
  void shutdown();
  bool running() const;

 private:
  bool ensureStarted(std::string *error = nullptr);
  json request(const std::string &action, const json &payload = json::object(), int timeout_ms = 5000);
  void handleEvent(const json &header, const std::vector<uint8_t> &payload);

  std::string project_root_;
  std::unique_ptr<StdioChannel> channel_;
  std::mutex callbacks_mu_;
  XrealBridgeCallbacks callbacks_;
};

}  // namespace recordlab::xreal_runtime
