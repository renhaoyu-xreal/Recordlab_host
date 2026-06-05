#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace recordlab::host::common {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& instance();

    void init(const std::string& log_dir,
              const std::string& ui_log_name,
              const std::string& all_log_name);
    std::string uiLogPath() const;
    std::string allLogPath() const;

    void appendUiLine(const std::string& line);
    void log(LogLevel level, const std::string& module, const std::string& message);
    void log(LogLevel level, const std::string& module, const std::string& message,
             const nlohmann::json& context);

private:
    Logger() = default;

    std::string formatAllLine(LogLevel level, const std::string& module, const std::string& message,
                              const nlohmann::json& context = nlohmann::json::object()) const;
    std::string levelString(LogLevel level) const;
    std::string timestampString() const;
    std::string threadIdString() const;

    mutable std::mutex mutex_;
    std::ofstream ui_stream_;
    std::ofstream all_stream_;
    std::string ui_log_path_;
    std::string all_log_path_;
};

}  // namespace recordlab::host::common
