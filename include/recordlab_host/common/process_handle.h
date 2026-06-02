#pragma once

#include <string>
#include <vector>

namespace recordlab::host {

class ProcessHandle {
public:
    ProcessHandle() = default;
    ~ProcessHandle();

    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    void start(const std::vector<std::string>& args,
               const std::string& cwd,
               const std::string& pythonpath);
    void terminate();
    int wait(int timeout_ms);
    int pid() const { return pid_; }

    /// Kill all processes whose command line contains `pattern`.
    /// Skips the calling process and pid 1.  Typically called at startup
    /// to clean up node_runtime processes left behind by a previous run.
    static void killByCmdlinePattern(const std::string& pattern);

private:
    int pid_ = -1;
};

}  // namespace recordlab::host
