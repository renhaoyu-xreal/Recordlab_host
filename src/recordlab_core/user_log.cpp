#include "recordlab_core/user_log.h"

#include "recordlab_master/registries.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace recordlab {
namespace {

std::tm localTime(std::time_t value) {
  std::tm tm{};
  localtime_r(&value, &tm);
  return tm;
}

std::string dateStamp(const std::tm &tm) {
  std::ostringstream out;
  out << std::put_time(&tm, "%Y%m%d");
  return out.str();
}

std::filesystem::path logRoot() {
  const char *env = std::getenv("RECORDLAB_LOG_ROOT");
  if (env && *env) return std::filesystem::path(env);
  return std::filesystem::path(RECORDLAB_MASTER_SOURCE_DIR) / "logs";
}

std::string sourceFileName(std::string source_node) {
  for (char &ch : source_node) {
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) ch = '_';
  }
  while (!source_node.empty() && source_node.front() == '_') source_node.erase(source_node.begin());
  return source_node.empty() ? "recordlab" : source_node;
}

void appendUserLogFile(const std::string &source_node, const json &payload) {
  try {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto dir = logRoot() / dateStamp(localTime(time)) / "user";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / (sourceFileName(source_node) + ".log"), std::ios::app);
    out << payload.dump() << "\n";
  } catch (...) {
  }
}

}  // namespace

std::string toString(UserLogLevel level) {
  switch (level) {
    case UserLogLevel::Info:
      return "INFO";
    case UserLogLevel::Warn:
      return "WARN";
    case UserLogLevel::Error:
      return "ERROR";
  }
  return "INFO";
}

UserLogLevel parseUserLogLevel(const std::string &level) {
  std::string normalized = level;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (normalized == "WARN" || normalized == "WARNING") return UserLogLevel::Warn;
  if (normalized == "ERROR" || normalized == "ERR") return UserLogLevel::Error;
  return UserLogLevel::Info;
}

UserLogPublisher::UserLogPublisher(std::string source_node, std::string topic)
    : source_node_(std::move(source_node)),
      topic_(std::move(topic)),
      publisher_(std::make_unique<Publisher>(topic_)) {}

std::string UserLogPublisher::endpoint() const {
  return publisher_ ? publisher_->endpoint() : "";
}

void UserLogPublisher::registerPublisher(MasterClient &client, const std::string &node_name) const {
  if (!publisher_) return;
  client.registerPublisher({{"node", node_name},
                            {"topic", topic_},
                            {"msg_type", "recordlab_msgs/UserLog"},
                            {"transport", {{"type", "tcp_pubsub"},
                                            {"endpoint", publisher_->endpoint()}}}});
}

void UserLogPublisher::publish(UserLogLevel level, const std::string &category,
                               const std::string &message, const json &details) {
  if (!publisher_) return;
  const json payload = {{"timestamp_ms", nowMs()},
                        {"source_node", source_node_},
                        {"level", toString(level)},
                        {"category", category},
                        {"message", message},
                        {"details", details.is_null() ? json::object() : details}};
  publisher_->publish(payload);
  appendUserLogFile(source_node_, payload);
}

void UserLogPublisher::info(const std::string &category, const std::string &message,
                            const json &details) {
  publish(UserLogLevel::Info, category, message, details);
}

void UserLogPublisher::warn(const std::string &category, const std::string &message,
                            const json &details) {
  publish(UserLogLevel::Warn, category, message, details);
}

void UserLogPublisher::error(const std::string &category, const std::string &message,
                             const json &details) {
  publish(UserLogLevel::Error, category, message, details);
}

}  // namespace recordlab
