#include "recordlab_host/common/logger.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / ("recordlab_logger_context_" + std::to_string(::getpid()));
    fs::create_directories(tmp);

    auto& logger = recordlab::host::common::Logger::instance();
    logger.init(tmp.string(), "ui.log", "all.log");
    logger.log(recordlab::host::common::LogLevel::Info, "LoggerTest", "plain message");
    logger.log(recordlab::host::common::LogLevel::Error, "LoggerTest", "context message", {
        {"request_id", "req_123"},
        {"agent_name", "imu_proxy"},
        {"cmd", "check"},
    });

    const auto all = readFile(tmp / "all.log");
    assert(all.find("plain message") != std::string::npos);
    assert(all.find("context message") != std::string::npos);
    assert(all.find("context=") != std::string::npos);
    assert(all.find("\"request_id\":\"req_123\"") != std::string::npos);
    assert(all.find("\"agent_name\":\"imu_proxy\"") != std::string::npos);
    assert(all.find("\"cmd\":\"check\"") != std::string::npos);

    fs::remove_all(tmp);
    std::cout << "logger context ok\n";
    return 0;
}
