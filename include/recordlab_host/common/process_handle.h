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

private:
    int pid_ = -1;
};

}  // namespace recordlab::host
