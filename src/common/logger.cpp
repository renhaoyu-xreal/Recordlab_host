#include "recordlab_host/common/logger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>

namespace recordlab::host::common {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& log_dir,
                  const std::string& ui_log_name,
                  const std::string& all_log_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(log_dir);
    ui_log_path_ = log_dir + "/" + ui_log_name;
    all_log_path_ = log_dir + "/" + all_log_name;
    if (ui_stream_.is_open()) {
        ui_stream_.close();
    }
    if (all_stream_.is_open()) {
        all_stream_.close();
    }
    ui_stream_.open(ui_log_path_, std::ios::app);
    all_stream_.open(all_log_path_, std::ios::app);
}

std::string Logger::uiLogPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ui_log_path_;
}

std::string Logger::allLogPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_log_path_;
}

void Logger::appendUiLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ui_stream_.is_open()) {
        return;
    }
    ui_stream_ << line << '\n';
    ui_stream_.flush();
}

void Logger::log(LogLevel level, const std::string& module, const std::string& message) {
    log(level, module, message, nlohmann::json::object());
}

void Logger::log(LogLevel level, const std::string& module, const std::string& message,
                 const nlohmann::json& context) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string line = formatAllLine(level, module, message, context);
    if (all_stream_.is_open()) {
        all_stream_ << line << '\n';
        all_stream_.flush();
    }
}

std::string Logger::formatAllLine(LogLevel level, const std::string& module, const std::string& message,
                                  const nlohmann::json& context) const {
    std::ostringstream oss;
    oss << '[' << timestampString() << ']'
        << " [" << levelString(level) << "]"
        << " [" << threadIdString() << "]"
        << " [" << module << "] "
        << message;
    if (!context.empty()) {
        oss << " context=" << context.dump();
    }
    return oss.str();
}

std::string Logger::levelString(LogLevel level) const {
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

std::string Logger::timestampString() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

std::string Logger::threadIdString() const {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

}  // namespace recordlab::host::common
