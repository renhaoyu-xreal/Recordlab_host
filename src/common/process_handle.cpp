#include "recordlab_host/common/process_handle.h"

#include <csignal>
#include <fcntl.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace recordlab::host {

ProcessHandle::~ProcessHandle() {
    terminate();
}

void ProcessHandle::start(const std::vector<std::string>& args,
                          const std::string& cwd,
                          const std::string& pythonpath) {
    if (args.empty()) {
        throw std::runtime_error("Process args cannot be empty");
    }
    pid_ = fork();
    if (pid_ < 0) {
        throw std::runtime_error("fork failed");
    }
    if (pid_ == 0) {
        int dev_null = open("/dev/null", O_RDWR);
        if (dev_null >= 0) {
            dup2(dev_null, STDIN_FILENO);
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            if (dev_null > STDERR_FILENO) {
                close(dev_null);
            }
        }
        if (!cwd.empty()) {
            chdir(cwd.c_str());
        }
        if (!pythonpath.empty()) {
            setenv("PYTHONPATH", pythonpath.c_str(), 1);
        }
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
}

void ProcessHandle::terminate() {
    if (pid_ <= 0) {
        return;
    }
    kill(pid_, SIGTERM);
    if (wait(3000) == -1 && pid_ > 0) {
        kill(pid_, SIGKILL);
        wait(1000);
    }
    pid_ = -1;
}

int ProcessHandle::wait(int timeout_ms) {
    if (pid_ <= 0) {
        return 0;
    }
    int status = 0;
    const int sleep_ms = 20;
    int waited = 0;
    while (waited <= timeout_ms) {
        pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            pid_ = -1;
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        waited += sleep_ms;
    }
    return -1;
}

}  // namespace recordlab::host
