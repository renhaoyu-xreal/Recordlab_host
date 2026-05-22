#include "recordlab_master/script_runner.h"

#include "recordlab_master/registries.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace recordlab {
namespace {

constexpr const char *kEventPrefix = "RECORDLAB_EVENT_JSON ";

std::string shellSafeArg(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '\n' || c == '\r') out.push_back(' ');
    else out.push_back(c);
  }
  return out;
}

std::vector<std::string> jsonStringArray(const json &value) {
  std::vector<std::string> out;
  if (!value.is_array()) return out;
  for (const auto &item : value) out.push_back(item.get<std::string>());
  return out;
}

}  // namespace

ScriptRunner::ScriptRunner(std::string master_endpoint, std::string shim_path)
    : master_endpoint_(std::move(master_endpoint)),
      shim_path_(std::move(shim_path)),
      client_(master_endpoint_) {
  current_status_ = {{"state", "idle"}, {"message", "脚本执行器空闲"}};
}

ScriptRunner::~ScriptRunner() { stop(); }

bool ScriptRunner::start() {
  if (running_.exchange(true)) return true;

  status_pub_ = std::make_unique<Publisher>("/script_runner/status");
  log_pub_ = std::make_unique<Publisher>("/script_runner/log");
  progress_pub_ = std::make_unique<Publisher>("/script_runner/progress");
  workflow_pub_ = std::make_unique<Publisher>("/script_runner/workflow");

  stop_service_ = std::make_unique<ServiceServer>(
      [this](const json &request) { return stopRequest(request); });
  run_action_ = std::make_unique<ActionServer>(
      [this](const json &goal, std::function<void(const json &)> feedback,
             std::atomic<bool> &cancelled) {
        return runGoal(goal, std::move(feedback), cancelled);
      });

  auto ok = client_.registerNode({{"node", "/script_runner"}, {"kind", "tool_node"}});
  if (!ok.value("ok", false)) {
    running_ = false;
    return false;
  }

  auto register_topic = [this](const std::string &topic, const std::string &msg_type,
                               const Publisher &publisher) {
    client_.registerPublisher({{"node", "/script_runner"},
                               {"topic", topic},
                               {"msg_type", msg_type},
                               {"transport", {{"type", "tcp_pubsub"},
                                               {"endpoint", publisher.endpoint()}}}});
  };
  register_topic("/script_runner/status", "recordlab_msgs/ScriptStatus", *status_pub_);
  register_topic("/script_runner/log", "recordlab_msgs/ScriptLog", *log_pub_);
  register_topic("/script_runner/progress", "recordlab_msgs/ScriptProgress", *progress_pub_);
  register_topic("/script_runner/workflow", "recordlab_msgs/ScriptWorkflow", *workflow_pub_);
  client_.registerService({{"node", "/script_runner"},
                           {"service", "/script_runner/stop_script"},
                           {"endpoint", stop_service_->endpoint()}});
  client_.registerAction({{"node", "/script_runner"},
                          {"action", "/script_runner/run_script"},
                          {"endpoints", run_action_->descriptor()}});

  publishStatus("idle", "脚本执行器已启动");
  return true;
}

void ScriptRunner::stop() {
  if (!running_.exchange(false)) return;
  try {
    stopRequest({{"reason", "script runner shutting down"}});
  } catch (...) {
  }
  try {
    client_.unregisterNode("/script_runner");
  } catch (...) {
  }
  run_action_.reset();
  stop_service_.reset();
  workflow_pub_.reset();
  progress_pub_.reset();
  log_pub_.reset();
  status_pub_.reset();
}

json ScriptRunner::status() const {
  std::lock_guard<std::mutex> lock(state_mu_);
  return current_status_;
}

json ScriptRunner::runGoal(const json &goal, std::function<void(const json &)> feedback,
                           std::atomic<bool> &cancelled) {
  if (script_running_.exchange(true)) {
    throw std::runtime_error("已有脚本正在运行");
  }

  const std::string script_path = goal.value("script_path", "");
  if (script_path.empty()) {
    script_running_ = false;
    throw std::runtime_error("缺少 script_path");
  }

  const json args = goal.value("args", json::array());
  const std::string main_agent = goal.value("main_agent", "");
  publishStatus("running", "脚本开始执行",
                {{"script_path", script_path}, {"main_agent", main_agent}});
  publishWorkflow({{"type", "workflow"},
                   {"action", "state"},
                   {"title", "脚本流程"},
                   {"steps", json::array()},
                   {"message", "脚本开始执行"},
                   {"finished", false},
                   {"success", nullptr}});

  json result = {{"script_path", script_path}, {"exit_code", -1}};
  try {
    int exit_code = runChildProcess(
        script_path, args,
        [this, &feedback](const std::string &line) {
          if (line.rfind(kEventPrefix, 0) == 0) {
            json event = json::parse(line.substr(std::strlen(kEventPrefix)));
            const std::string type = event.value("type", "");
            if (type == "progress") publishProgress(event);
            else if (type == "workflow") publishWorkflow(event);
            else if (type == "log") publishLog(event.value("stream", "stdout"), event.value("message", ""));
            feedback(event);
            return;
          }
          publishLog("stdout", line);
          feedback({{"type", "log"}, {"stream", "stdout"}, {"message", line}});
        },
        cancelled);
    result["exit_code"] = exit_code;
    const bool success = exit_code == 0 && !cancelled;
    result["success"] = success;
    publishStatus(success ? "idle" : "failed",
                  success ? "脚本执行完成" : "脚本执行失败",
                  {{"script_path", script_path}, {"exit_code", exit_code}});
    publishWorkflow({{"type", "workflow"},
                     {"action", "state"},
                     {"title", "脚本流程"},
                     {"steps", json::array()},
                     {"message", success ? "脚本执行完成" : "脚本执行失败"},
                     {"finished", true},
                     {"success", success}});
  } catch (const std::exception &e) {
    result["success"] = false;
    result["error"] = e.what();
    publishLog("stderr", e.what());
    publishStatus("failed", e.what(), {{"script_path", script_path}});
  }
  {
    std::lock_guard<std::mutex> lock(state_mu_);
    child_pid_ = -1;
  }
  script_running_ = false;
  return result;
}

json ScriptRunner::stopRequest(const json &request) {
  pid_t pid = -1;
  {
    std::lock_guard<std::mutex> lock(state_mu_);
    pid = child_pid_;
  }
  if (pid > 0) {
    kill(pid, SIGTERM);
    publishStatus("stopping", request.value("reason", "用户请求停止脚本"));
    return {{"stopped", true}, {"pid", pid}};
  }
  return {{"stopped", false}, {"message", "没有正在运行的脚本"}};
}

void ScriptRunner::publishStatus(const std::string &state, const std::string &message,
                                 const json &extra) {
  json payload = extra;
  payload["state"] = state;
  payload["message"] = message;
  payload["timestamp_ms"] = nowMs();
  {
    std::lock_guard<std::mutex> lock(state_mu_);
    current_status_ = payload;
  }
  if (status_pub_) status_pub_->publish(payload);
}

void ScriptRunner::publishLog(const std::string &stream, const std::string &message) {
  if (!log_pub_) return;
  log_pub_->publish({{"stream", stream}, {"message", message}, {"timestamp_ms", nowMs()}});
}

void ScriptRunner::publishProgress(const json &progress) {
  if (progress_pub_) progress_pub_->publish(progress);
}

void ScriptRunner::publishWorkflow(const json &workflow) {
  if (workflow_pub_) workflow_pub_->publish(workflow);
}

int ScriptRunner::runChildProcess(const std::string &script_path, const json &args,
                                  std::function<void(const std::string &)> on_line,
                                  std::atomic<bool> &cancelled) {
  if (!std::filesystem::exists(script_path)) {
    throw std::runtime_error("脚本不存在: " + script_path);
  }
  const std::string shim = resolveShimPath();
  if (!std::filesystem::exists(shim)) {
    throw std::runtime_error("脚本 shim 不存在: " + shim);
  }

  int pipe_fd[2];
  if (pipe(pipe_fd) != 0) throw std::runtime_error("创建脚本管道失败");

  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    throw std::runtime_error("fork 脚本进程失败");
  }

  if (pid == 0) {
    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    dup2(pipe_fd[1], STDERR_FILENO);
    close(pipe_fd[1]);

    std::vector<std::string> argv_strings = {"python3", "-u", shim, shellSafeArg(script_path)};
    for (const auto &arg : jsonStringArray(args)) argv_strings.push_back(shellSafeArg(arg));
    std::vector<char *> argv;
    for (auto &arg : argv_strings) argv.push_back(arg.data());
    argv.push_back(nullptr);
    execvp("python3", argv.data());
    _exit(127);
  }

  close(pipe_fd[1]);
  {
    std::lock_guard<std::mutex> lock(state_mu_);
    child_pid_ = pid;
  }

  std::string buffer;
  char chunk[512];
  while (true) {
    if (cancelled) kill(pid, SIGTERM);
    ssize_t n = read(pipe_fd[0], chunk, sizeof(chunk));
    if (n > 0) {
      buffer.append(chunk, chunk + n);
      size_t pos = 0;
      while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        on_line(line);
        buffer.erase(0, pos + 1);
      }
      continue;
    }
    if (n == 0) break;
    if (errno == EINTR) continue;
    break;
  }
  if (!buffer.empty()) on_line(buffer);
  close(pipe_fd[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
}

std::string ScriptRunner::resolveShimPath() const {
  if (!shim_path_.empty()) return shim_path_;
  const std::filesystem::path source_dir = std::filesystem::path(RECORDLAB_MASTER_SOURCE_DIR);
  return (source_dir / "tools" / "recordlab_script_shim.py").string();
}

}  // namespace recordlab
