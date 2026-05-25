#pragma once

#include "recordlab_echo/echo.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>

namespace recordlab {

class StdioChannel {
 public:
  using EventCallback = std::function<void(const json &, const std::vector<uint8_t> &)>;

  StdioChannel() = default;
  ~StdioChannel();
  StdioChannel(const StdioChannel &) = delete;
  StdioChannel &operator=(const StdioChannel &) = delete;

  bool start(const std::string &program, const std::vector<std::string> &args,
             const std::map<std::string, std::string> &env = {});
  void stop();
  bool running() const { return running_; }
  void setEventCallback(EventCallback cb);
  json request(const std::string &action, const json &payload = json::object(),
               int timeout_ms = 5000);

 private:
  bool writeFrame(const json &header, const std::vector<uint8_t> &payload = {});
  bool writeAll(const uint8_t *data, size_t size);
  void readerLoop();
  void stderrLoop();
  void handleFrame(const json &header, const std::vector<uint8_t> &payload);
  void closeFds();

  int stdin_fd_{-1};
  int stdout_fd_{-1};
  int stderr_fd_{-1};
  pid_t pid_{-1};
  std::atomic<bool> running_{false};
  std::thread reader_thread_;
  std::thread stderr_thread_;
  std::mutex request_mu_;
  std::mutex response_mu_;
  std::condition_variable response_cv_;
  std::map<std::string, json> responses_;
  std::atomic<uint64_t> next_request_id_{1};
  std::mutex event_mu_;
  EventCallback event_cb_;
  std::mutex stderr_mu_;
  std::string last_stderr_;
};

}  // namespace recordlab
