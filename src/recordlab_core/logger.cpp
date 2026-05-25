#include "recordlab_core/logger.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace recordlab {
namespace {

std::mutex &logMutex() {
  static std::mutex mu;
  return mu;
}

std::string &componentName() {
  static std::string component = "recordlab";
  return component;
}

std::string levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "INFO";
}

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

std::string timeStamp(const std::tm &tm) {
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::filesystem::path logRoot() {
  const char *env = std::getenv("RECORDLAB_LOG_ROOT");
  if (env && *env) return std::filesystem::path(env);
  return std::filesystem::path(RECORDLAB_MASTER_SOURCE_DIR) / "logs";
}

}  // namespace

void setLogComponent(std::string component) {
  std::lock_guard<std::mutex> lock(logMutex());
  if (!component.empty()) componentName() = std::move(component);
}

void logMessage(LogLevel level, const char *file, int line, const std::string &message) {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  const auto tm = localTime(time);

  std::lock_guard<std::mutex> lock(logMutex());
  const std::string component = componentName();
  std::ostringstream record;
  record << timeStamp(tm) << " [" << levelName(level) << "] [" << component << "] "
         << message << " (" << file << ":" << line << ")";

  std::cerr << record.str() << "\n";

  try {
    const auto dir = logRoot() / dateStamp(tm) / "system";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / (component + ".log"), std::ios::app);
    out << record.str() << "\n";
  } catch (...) {
  }
}

}  // namespace recordlab
