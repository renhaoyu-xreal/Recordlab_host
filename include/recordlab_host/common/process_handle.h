#pragma once

#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace recordlab::host {

class ProcessHandle {
public:
    using OutputCallback = std::function<void(nlohmann::json)>;

    struct Metadata {
        std::string process;
        std::string agent_name;
        std::string node_name;
    };

    ProcessHandle() = default;
    ~ProcessHandle();

    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    void start(const std::vector<std::string>& args,
               const std::string& cwd,
               const std::string& pythonpath);
    void start(const std::vector<std::string>& args,
               const std::string& cwd,
               const std::string& pythonpath,
               Metadata metadata,
               OutputCallback output_callback);
    void terminate();
    int wait(int timeout_ms);
    int pid() const { return pid_; }

    /// Kill all processes whose command line contains `pattern`.
    /// Skips the calling process and pid 1.  Typically called at startup
    /// to clean up node_runtime processes left behind by a previous run.
    static void killByCmdlinePattern(const std::string& pattern);

private:
    void startImpl(const std::vector<std::string>& args,
                   const std::string& cwd,
                   const std::string& pythonpath,
                   Metadata metadata,
                   OutputCallback output_callback);
    void stopOutputReaders();
    void readOutputLoop(int fd, std::string stream);
    nlohmann::json contextWithPid(int pid) const;

    int pid_ = -1;
    Metadata metadata_;
    OutputCallback output_callback_;
    std::thread stdout_thread_;
    std::thread stderr_thread_;
};

}  // namespace recordlab::host
