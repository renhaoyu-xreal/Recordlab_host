#pragma once

#include "recordlab_master/master_client.h"
#include "recordlab_master/transport.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <sys/types.h>

namespace recordlab {

class ScriptRunner {
 public:
  explicit ScriptRunner(std::string master_endpoint = "tcp://127.0.0.1:5590",
                        std::string shim_path = "");
  ~ScriptRunner();

  bool start();
  void stop();
  bool running() const { return running_; }
  json status() const;

 private:
  json runGoal(const json &goal, std::function<void(const json &)> feedback,
               std::atomic<bool> &cancelled);
  json stopRequest(const json &request);
  void publishStatus(const std::string &state, const std::string &message,
                     const json &extra = json::object());
  void publishLog(const std::string &stream, const std::string &message);
  void publishProgress(const json &progress);
  void publishWorkflow(const json &workflow);
  int runChildProcess(const std::string &script_path, const json &args,
                      std::function<void(const std::string &)> on_line,
                      std::atomic<bool> &cancelled);
  std::string resolveShimPath() const;

  std::string master_endpoint_;
  std::string shim_path_;
  MasterClient client_;
  std::atomic<bool> running_{false};
  std::atomic<bool> script_running_{false};
  mutable std::mutex state_mu_;
  json current_status_;
  pid_t child_pid_{-1};

  std::unique_ptr<Publisher> status_pub_;
  std::unique_ptr<Publisher> log_pub_;
  std::unique_ptr<Publisher> progress_pub_;
  std::unique_ptr<Publisher> workflow_pub_;
  std::unique_ptr<ServiceServer> stop_service_;
  std::unique_ptr<ActionServer> run_action_;
};

}  // namespace recordlab
