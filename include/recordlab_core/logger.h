#pragma once

#include <string>

namespace recordlab {

enum class LogLevel {
  Debug,
  Info,
  Warn,
  Error,
};

void setLogComponent(std::string component);
void logMessage(LogLevel level, const char *file, int line, const std::string &message);

}  // namespace recordlab

#define RL_LOG_DEBUG(message) ::recordlab::logMessage(::recordlab::LogLevel::Debug, __FILE__, __LINE__, (message))
#define RL_LOG_INFO(message) ::recordlab::logMessage(::recordlab::LogLevel::Info, __FILE__, __LINE__, (message))
#define RL_LOG_WARN(message) ::recordlab::logMessage(::recordlab::LogLevel::Warn, __FILE__, __LINE__, (message))
#define RL_LOG_ERROR(message) ::recordlab::logMessage(::recordlab::LogLevel::Error, __FILE__, __LINE__, (message))
