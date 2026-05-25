#include "recordlab_echo/echo.h"

#include <chrono>
#include <cstring>
#include <stdexcept>

namespace recordlab {

int bindRandomTcpPort(zmq::socket_t &socket) {
  socket.bind("tcp://127.0.0.1:*");
  std::string ep = socket.get(zmq::sockopt::last_endpoint);
  return std::stoi(ep.substr(ep.rfind(':') + 1));
}

Publisher::Publisher(std::string topic) : topic_(std::move(topic)) {
  socket_.set(zmq::sockopt::linger, 0);
  port_ = bindRandomTcpPort(socket_);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

Publisher::~Publisher() {
  socket_.close();
  context_.close();
}

void Publisher::publish(const json &payload) {
  std::lock_guard<std::mutex> lock(mu_);
  const std::string text = topic_ + " " + payload.dump();
  socket_.send(zmq::buffer(text), zmq::send_flags::dontwait);
}

Subscriber::Subscriber(std::string endpoint, std::string topic, Callback cb)
    : topic_(std::move(topic)), cb_(std::move(cb)) {
  socket_.set(zmq::sockopt::linger, 0);
  socket_.set(zmq::sockopt::subscribe, topic_);
  socket_.connect(endpoint);
  thread_ = std::thread(&Subscriber::loop, this);
}

Subscriber::~Subscriber() {
  running_ = false;
  socket_.close();
  context_.close();
  if (thread_.joinable()) thread_.join();
}

void Subscriber::loop() {
  while (running_) {
    try {
      zmq::message_t msg;
      auto ok = socket_.recv(msg, zmq::recv_flags::dontwait);
      if (!ok) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      std::string text(static_cast<char *>(msg.data()), msg.size());
      auto pos = text.find(' ');
      if (pos == std::string::npos) continue;
      cb_(json::parse(text.substr(pos + 1)));
    } catch (...) {
    }
  }
}

ServiceServer::ServiceServer(Callback cb) : cb_(std::move(cb)) {
  socket_.set(zmq::sockopt::linger, 0);
  port_ = bindRandomTcpPort(socket_);
  thread_ = std::thread(&ServiceServer::loop, this);
}

ServiceServer::~ServiceServer() {
  running_ = false;
  socket_.close();
  context_.close();
  if (thread_.joinable()) thread_.join();
}

void ServiceServer::loop() {
  while (running_) {
    try {
      zmq::message_t req;
      auto ok = socket_.recv(req, zmq::recv_flags::dontwait);
      if (!ok) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      json request = json::parse(std::string(static_cast<char *>(req.data()), req.size()));
      json response;
      try {
        response = {{"ok", true}, {"data", cb_(request)}, {"error", ""}};
      } catch (const std::exception &e) {
        response = {{"ok", false}, {"data", nullptr}, {"error", e.what()}};
      }
      const std::string text = response.dump();
      socket_.send(zmq::buffer(text), zmq::send_flags::none);
    } catch (...) {
    }
  }
}

ServiceClient::ServiceClient(std::string endpoint, int timeout_ms)
    : endpoint_(std::move(endpoint)), timeout_ms_(timeout_ms) {
  resetSocket();
}

ServiceClient::~ServiceClient() {
  if (socket_) socket_->close();
  context_.close();
}

void ServiceClient::resetSocket() {
  if (socket_) socket_->close();
  socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::req);
  socket_->set(zmq::sockopt::linger, 0);
  socket_->set(zmq::sockopt::rcvtimeo, timeout_ms_);
  socket_->set(zmq::sockopt::sndtimeo, timeout_ms_);
  socket_->connect(endpoint_);
}

json ServiceClient::call(const json &request) {
  std::lock_guard<std::mutex> lock(mu_);
  try {
    const std::string text = request.dump();
    socket_->send(zmq::buffer(text), zmq::send_flags::none);
    zmq::message_t reply;
    auto ok = socket_->recv(reply, zmq::recv_flags::none);
    if (!ok) {
      resetSocket();
      throw std::runtime_error("service timeout");
    }
    return json::parse(std::string(static_cast<char *>(reply.data()), reply.size()));
  } catch (...) {
    resetSocket();
    throw;
  }
}

ActionServer::ActionServer(GoalCallback cb)
    : cb_(std::move(cb)),
      goal_([this](const json &req) {
        uint64_t id = next_goal_id_++;
        auto flag = std::make_shared<std::atomic<bool>>(false);
        {
          std::lock_guard<std::mutex> lock(goals_mu_);
          cancel_flags_[id] = flag;
        }
        std::thread([this, id, flag, goal = req.value("goal", json::object())]() {
          auto feedback = [this, id](const json &fb) { feedback_.publish({{"goal_id", id}, {"feedback", fb}}); };
          json data;
          bool ok = true;
          try {
            data = cb_(goal, feedback, *flag);
          } catch (const std::exception &e) {
            ok = false;
            data = {{"error", e.what()}};
          }
          result_.publish({{"goal_id", id}, {"ok", ok}, {"data", data}});
          std::lock_guard<std::mutex> lock(goals_mu_);
          cancel_flags_.erase(id);
        }).detach();
        return json{{"goal_id", id}, {"accepted", true}};
      }),
      cancel_([this](const json &req) {
        uint64_t id = req.value("goal_id", 0UL);
        std::lock_guard<std::mutex> lock(goals_mu_);
        auto it = cancel_flags_.find(id);
        if (it == cancel_flags_.end()) return json{{"cancelled", false}};
        *(it->second) = true;
        return json{{"cancelled", true}};
      }),
      feedback_("feedback"),
      result_("result") {}

json ActionServer::descriptor() const {
  return {{"send_goal", goal_.endpoint()},
          {"cancel", cancel_.endpoint()},
          {"feedback", feedback_.endpoint()},
          {"feedback_topic", "feedback"},
          {"result", result_.endpoint()},
          {"result_topic", "result"}};
}

ActionClient::ActionClient(json descriptor, int timeout_ms)
    : descriptor_(std::move(descriptor)),
      goal_(descriptor_.at("send_goal").get<std::string>(), timeout_ms),
      cancel_(descriptor_.at("cancel").get<std::string>(), timeout_ms) {
  result_sub_ = std::make_unique<Subscriber>(
      descriptor_.at("result").get<std::string>(),
      descriptor_.value("result_topic", "result"),
      [this](const json &msg) {
        std::lock_guard<std::mutex> lock(results_mu_);
        results_[msg.value("goal_id", 0UL)] = msg;
      });
}

uint64_t ActionClient::sendGoal(const json &goal) {
  auto resp = goal_.call({{"goal", goal}});
  if (!resp.value("ok", false)) throw std::runtime_error(resp.value("error", "goal rejected"));
  return resp["data"].value("goal_id", 0UL);
}

json ActionClient::waitForResult(uint64_t goal_id, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    {
      std::lock_guard<std::mutex> lock(results_mu_);
      auto it = results_.find(goal_id);
      if (it != results_.end()) return it->second;
    }
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() > timeout_ms) {
      throw std::runtime_error("action result timeout");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

json ActionClient::cancel(uint64_t goal_id) { return cancel_.call({{"goal_id", goal_id}}); }

}  // namespace recordlab
