#pragma once

#include "recordlab_core/master_client.h"
#include "recordlab_echo/echo.h"

#include <memory>
#include <string>

namespace recordlab {

enum class UserLogLevel {
  Info,
  Warn,
  Error,
};

std::string toString(UserLogLevel level);
UserLogLevel parseUserLogLevel(const std::string &level);

class UserLogPublisher {
 public:
  explicit UserLogPublisher(std::string source_node,
                            std::string topic = "/recordlab/user_log");

  const std::string &topic() const { return topic_; }
  const std::string &sourceNode() const { return source_node_; }
  std::string endpoint() const;
  void registerPublisher(MasterClient &client, const std::string &node_name) const;
  void publish(UserLogLevel level, const std::string &category, const std::string &message,
               const json &details = json::object());
  void info(const std::string &category, const std::string &message,
            const json &details = json::object());
  void warn(const std::string &category, const std::string &message,
            const json &details = json::object());
  void error(const std::string &category, const std::string &message,
             const json &details = json::object());

 private:
  std::string source_node_;
  std::string topic_;
  std::unique_ptr<Publisher> publisher_;
};

}  // namespace recordlab
