#include "recordlab_xreal_runtime/xreal_bridge_client.h"

#include <cstdlib>
#include <map>
#include <utility>

namespace recordlab::xreal_runtime {

namespace {

std::string executableOrDefault(const char *env_name, const std::string &fallback) {
  const char *value = std::getenv(env_name);
  return value && *value ? std::string(value) : fallback;
}

}  // namespace

XrealBridgeClient::XrealBridgeClient(std::string project_root)
    : project_root_(std::move(project_root)) {}

XrealBridgeClient::~XrealBridgeClient() { shutdown(); }

void XrealBridgeClient::setCallbacks(XrealBridgeCallbacks callbacks) {
  std::lock_guard<std::mutex> lock(callbacks_mu_);
  callbacks_ = std::move(callbacks);
  if (channel_) {
    channel_->setEventCallback([this](const json &header, const std::vector<uint8_t> &payload) {
      handleEvent(header, payload);
    });
  }
}

bool XrealBridgeClient::running() const {
  return channel_ && channel_->running();
}

json XrealBridgeClient::enumerateDevices() { return request("enumerate_devices", json::object(), 5000); }
json XrealBridgeClient::createGlasses(int timeout_ms) { return request("create_glasses", json::object(), timeout_ms); }
json XrealBridgeClient::openGlasses(int timeout_ms) { return request("open_glasses", json::object(), timeout_ms); }
json XrealBridgeClient::configureGlasses(const json &params, int timeout_ms) {
  return request("configure_glasses", params, timeout_ms);
}
json XrealBridgeClient::startSensors(int sensor_mask, int timeout_ms) {
  return request("start_sensors", {{"sensor_mask", sensor_mask}}, timeout_ms);
}
json XrealBridgeClient::stopSensors(int sensor_mask, int timeout_ms) {
  return request("stop_sensors", {{"sensor_mask", sensor_mask}}, timeout_ms);
}
json XrealBridgeClient::getGlassesState(int timeout_ms) {
  return request("get_glasses_state", json::object(), timeout_ms);
}
json XrealBridgeClient::closeGlasses(int timeout_ms) {
  return request("close_glasses", json::object(), timeout_ms);
}

bool XrealBridgeClient::ensureStarted(std::string *error) {
  if (running()) return true;
  channel_ = std::make_unique<StdioChannel>();
  channel_->setEventCallback([this](const json &header, const std::vector<uint8_t> &payload) {
    handleEvent(header, payload);
  });

  const std::string python = executableOrDefault("RECORDLAB_XREAL_PYTHON", "/usr/bin/python3");
  const std::string script = project_root_ + "/third_party/xreal/scripts/xreal_bridge_worker.py";
  const std::map<std::string, std::string> env = {
      {"PYTHONUNBUFFERED", "1"},
      {"RECORDLAB_MASTER_ROOT", project_root_},
      {"RECORDLABC_ROOT", project_root_},
  };
  if (!channel_->start(python, {script, "--project-root", project_root_}, env)) {
    if (error) *error = "启动 XREAL bridge worker 失败: " + script;
    channel_.reset();
    return false;
  }
  return true;
}

json XrealBridgeClient::request(const std::string &action, const json &payload, int timeout_ms) {
  std::string error;
  if (!ensureStarted(&error)) return {{"success", false}, {"message", error}};
  return channel_->request(action, payload, timeout_ms);
}

void XrealBridgeClient::shutdown() {
  if (channel_) {
    channel_->stop();
    channel_.reset();
  }
}

void XrealBridgeClient::handleEvent(const json &header, const std::vector<uint8_t> &payload) {
  const std::string event = header.value("event", "");
  const json body = header.value("payload", json::object());
  XrealBridgeCallbacks callbacks;
  {
    std::lock_guard<std::mutex> lock(callbacks_mu_);
    callbacks = callbacks_;
  }
  if (event == "imu_batch" && callbacks.imu_batch) {
    callbacks.imu_batch(body);
  } else if (event == "camera" && callbacks.camera) {
    callbacks.camera(body, payload);
  }
}

}  // namespace recordlab::xreal_runtime
