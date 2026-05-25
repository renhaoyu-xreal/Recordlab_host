#include "recordlab_echo/stdio_channel.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace recordlab {
namespace {

constexpr uint8_t kMagic[4] = {'R', 'L', 'C', 'B'};
constexpr size_t kPrefixSize = 12;

void appendU32(std::vector<uint8_t> &out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

uint32_t readU32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8u) |
         (static_cast<uint32_t>(data[2]) << 16u) |
         (static_cast<uint32_t>(data[3]) << 24u);
}

bool readExact(int fd, uint8_t *data, size_t size, const std::atomic<bool> &running) {
  size_t offset = 0;
  while (offset < size && running) {
    const ssize_t n = ::read(fd, data + offset, size - offset);
    if (n > 0) {
      offset += static_cast<size_t>(n);
    } else if (n == 0) {
      return false;
    } else if (errno != EINTR) {
      return false;
    }
  }
  return offset == size;
}

}  // namespace

StdioChannel::~StdioChannel() { stop(); }

bool StdioChannel::start(const std::string &program, const std::vector<std::string> &args,
                         const std::map<std::string, std::string> &env) {
  stop();
  int in_pipe[2]{-1, -1};
  int out_pipe[2]{-1, -1};
  int err_pipe[2]{-1, -1};
  if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) return false;

  pid_t child = ::fork();
  if (child < 0) return false;
  if (child == 0) {
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::dup2(err_pipe[1], STDERR_FILENO);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[0]);
    ::close(err_pipe[1]);
    for (const auto &kv : env) setenv(kv.first.c_str(), kv.second.c_str(), 1);

    std::vector<std::string> argv_strings;
    argv_strings.reserve(args.size() + 1);
    argv_strings.push_back(program);
    argv_strings.insert(argv_strings.end(), args.begin(), args.end());
    std::vector<char *> argv;
    argv.reserve(argv_strings.size() + 1);
    for (auto &arg : argv_strings) argv.push_back(arg.data());
    argv.push_back(nullptr);
    execvp(program.c_str(), argv.data());
    _exit(127);
  }

  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  ::close(err_pipe[1]);
  stdin_fd_ = in_pipe[1];
  stdout_fd_ = out_pipe[0];
  stderr_fd_ = err_pipe[0];
  pid_ = child;
  running_ = true;
  reader_thread_ = std::thread(&StdioChannel::readerLoop, this);
  stderr_thread_ = std::thread(&StdioChannel::stderrLoop, this);
  return true;
}

void StdioChannel::stop() {
  if (running_) {
    try {
      request("shutdown", json::object(), 500);
    } catch (...) {
    }
  }
  running_ = false;
  response_cv_.notify_all();
  closeFds();
  if (pid_ > 0) {
    ::kill(pid_, SIGTERM);
    int status = 0;
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
  }
  if (reader_thread_.joinable()) reader_thread_.join();
  if (stderr_thread_.joinable()) stderr_thread_.join();
  responses_.clear();
}

void StdioChannel::setEventCallback(EventCallback cb) {
  std::lock_guard<std::mutex> lock(event_mu_);
  event_cb_ = std::move(cb);
}

json StdioChannel::request(const std::string &action, const json &payload, int timeout_ms) {
  std::lock_guard<std::mutex> req_lock(request_mu_);
  if (!running_) return {{"success", false}, {"message", "stdio channel not running"}};
  const std::string id = std::to_string(next_request_id_++);
  if (!writeFrame({{"type", "request"}, {"id", id}, {"action", action}, {"payload", payload}})) {
    return {{"success", false}, {"message", "failed to write stdio request"}};
  }
  std::unique_lock<std::mutex> lock(response_mu_);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  if (!response_cv_.wait_until(lock, deadline, [&] {
        return !running_ || responses_.find(id) != responses_.end();
      })) {
    std::string err;
    {
      std::lock_guard<std::mutex> stderr_lock(stderr_mu_);
      err = last_stderr_;
    }
    return {{"success", false}, {"message", err.empty() ? "stdio request timeout: " + action : err}};
  }
  auto it = responses_.find(id);
  if (it == responses_.end()) return {{"success", false}, {"message", "stdio worker exited"}};
  json response = it->second;
  responses_.erase(it);
  return response;
}

bool StdioChannel::writeFrame(const json &header, const std::vector<uint8_t> &payload) {
  const std::string text = header.dump();
  std::vector<uint8_t> frame;
  frame.reserve(kPrefixSize + text.size() + payload.size());
  frame.insert(frame.end(), std::begin(kMagic), std::end(kMagic));
  appendU32(frame, static_cast<uint32_t>(text.size()));
  appendU32(frame, static_cast<uint32_t>(payload.size()));
  frame.insert(frame.end(), text.begin(), text.end());
  frame.insert(frame.end(), payload.begin(), payload.end());
  return writeAll(frame.data(), frame.size());
}

bool StdioChannel::writeAll(const uint8_t *data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    const ssize_t n = ::write(stdin_fd_, data + offset, size - offset);
    if (n > 0) offset += static_cast<size_t>(n);
    else if (n < 0 && errno == EINTR) continue;
    else return false;
  }
  return true;
}

void StdioChannel::readerLoop() {
  while (running_) {
    uint8_t prefix[kPrefixSize]{};
    if (!readExact(stdout_fd_, prefix, kPrefixSize, running_)) break;
    if (!std::equal(std::begin(kMagic), std::end(kMagic), prefix)) continue;
    const uint32_t header_size = readU32(prefix + 4);
    const uint32_t payload_size = readU32(prefix + 8);
    std::vector<uint8_t> header_bytes(header_size);
    std::vector<uint8_t> payload(payload_size);
    if (!readExact(stdout_fd_, header_bytes.data(), header_size, running_)) break;
    if (payload_size > 0 && !readExact(stdout_fd_, payload.data(), payload_size, running_)) break;
    try {
      handleFrame(json::parse(std::string(header_bytes.begin(), header_bytes.end())), payload);
    } catch (...) {
    }
  }
  running_ = false;
  response_cv_.notify_all();
}

void StdioChannel::stderrLoop() {
  char buffer[512];
  while (running_) {
    const ssize_t n = ::read(stderr_fd_, buffer, sizeof(buffer));
    if (n > 0) {
      std::lock_guard<std::mutex> lock(stderr_mu_);
      last_stderr_.assign(buffer, buffer + n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
}

void StdioChannel::handleFrame(const json &header, const std::vector<uint8_t> &payload) {
  const std::string type = header.value("type", "");
  if (type == "response") {
    const std::string id = header.value("id", "");
    std::lock_guard<std::mutex> lock(response_mu_);
    responses_[id] = header.value("result", json::object());
    response_cv_.notify_all();
    return;
  }
  if (type == "event") {
    EventCallback cb;
    {
      std::lock_guard<std::mutex> lock(event_mu_);
      cb = event_cb_;
    }
    if (cb) cb(header, payload);
  }
}

void StdioChannel::closeFds() {
  if (stdin_fd_ >= 0) ::close(stdin_fd_);
  if (stdout_fd_ >= 0) ::close(stdout_fd_);
  if (stderr_fd_ >= 0) ::close(stderr_fd_);
  stdin_fd_ = stdout_fd_ = stderr_fd_ = -1;
}

}  // namespace recordlab
