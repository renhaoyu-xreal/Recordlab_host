#include "recordlab_host/common/process_handle.h"
#include "recordlab_host/common/logger.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

namespace recordlab::host {
namespace {

std::vector<std::string> buildEnvironment(const std::string& pythonpath) {
    std::vector<std::string> env;
    const std::string key = "PYTHONPATH=";
    bool found_pythonpath = false;

    for (char** current = environ; current != nullptr && *current != nullptr; ++current) {
        std::string entry(*current);
        if (entry.rfind(key, 0) == 0) {
            found_pythonpath = true;
            if (!pythonpath.empty()) {
                entry = key + pythonpath;
            }
        }
        env.push_back(std::move(entry));
    }

    if (!pythonpath.empty() && !found_pythonpath) {
        env.push_back(key + pythonpath);
    }
    return env;
}

void setEnvironmentValue(std::vector<std::string>& env,
                         const std::string& key,
                         const std::string& value) {
    const std::string prefix = key + "=";
    for (auto& entry : env) {
        if (entry.rfind(prefix, 0) == 0) {
            entry = prefix + value;
            return;
        }
    }
    env.push_back(prefix + value);
}

std::runtime_error spawnError(const char* action, int error_code) {
    return std::runtime_error(std::string(action) + " failed: " + std::strerror(error_code));
}

std::vector<std::string> splitPath(const char* path_env) {
    const std::string path = path_env && *path_env ? path_env : "/usr/local/bin:/usr/bin:/bin";
    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string item;
    while (std::getline(stream, item, ':')) {
        parts.push_back(item.empty() ? "." : item);
    }
    return parts;
}

std::string resolveExecutable(const std::string& program) {
    if (program.empty()) {
        throw std::runtime_error("Process executable cannot be empty");
    }
    if (program.find('/') != std::string::npos) {
        if (::access(program.c_str(), X_OK) == 0) {
            return program;
        }
        const int error_code = errno;
        throw std::runtime_error("Executable is not runnable: " + program +
                                 ": " + std::strerror(error_code));
    }
    for (const auto& dir : splitPath(std::getenv("PATH"))) {
        std::string candidate = dir + "/" + program;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }
    throw std::runtime_error("Executable not found in PATH: " + program);
}

std::vector<char*> mutablePointers(std::vector<std::string>& values) {
    std::vector<char*> pointers;
    pointers.reserve(values.size() + 1);
    for (auto& value : values) {
        pointers.push_back(value.data());
    }
    pointers.push_back(nullptr);
    return pointers;
}

std::string formatArgs(const std::vector<std::string>& args) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        if (args[i].find_first_of(" \t\n\"'") == std::string::npos) {
            oss << args[i];
        } else {
            oss << '"';
            for (const char c : args[i]) {
                if (c == '"' || c == '\\') {
                    oss << '\\';
                }
                oss << c;
            }
            oss << '"';
        }
    }
    return oss.str();
}

void writeExecErrorAndExit(int fd, int error_code) {
    const int saved_errno = error_code;
    ssize_t ignored = ::write(fd, &saved_errno, sizeof(saved_errno));
    (void)ignored;
    _exit(127);
}

}  // namespace

ProcessHandle::~ProcessHandle() {
    terminate();
}

void ProcessHandle::start(const std::vector<std::string>& args,
                          const std::string& cwd,
                          const std::string& pythonpath) {
    if (args.empty()) {
        throw std::runtime_error("Process args cannot be empty");
    }

    terminate();

    std::vector<std::string> argv_storage = args;
    argv_storage[0] = resolveExecutable(argv_storage[0]);
    std::vector<char*> argv = mutablePointers(argv_storage);
    std::vector<std::string> env_storage = buildEnvironment(pythonpath);
    setEnvironmentValue(env_storage, "PYTHONUNBUFFERED", "1");
    std::vector<char*> envp = mutablePointers(env_storage);
    const std::string child_output_path = common::Logger::instance().allLogPath();
    common::Logger::instance().log(
        common::LogLevel::Info,
        "ProcessHandle",
        "starting process: argv=" + formatArgs(argv_storage) +
            ", cwd=" + (cwd.empty() ? std::string("<inherit>") : cwd) +
            ", stdout_stderr=" + (child_output_path.empty() ? std::string("/dev/null") : child_output_path));

    int error_pipe[2] = {-1, -1};
    if (::pipe2(error_pipe, O_CLOEXEC) != 0) {
        throw spawnError("pipe2", errno);
    }

    pid_t child_pid = ::vfork();
    if (child_pid < 0) {
        const int error_code = errno;
        ::close(error_pipe[0]);
        ::close(error_pipe[1]);
        throw spawnError("vfork", error_code);
    }

    if (child_pid == 0) {
        ::close(error_pipe[0]);
        if (::setpgid(0, 0) != 0) {
            writeExecErrorAndExit(error_pipe[1], errno);
        }
        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0) {
            writeExecErrorAndExit(error_pipe[1], errno);
        }
        if (::getppid() == 1) {
            writeExecErrorAndExit(error_pipe[1], ESRCH);
        }
        const char* output_path = child_output_path.empty() ? "/dev/null" : child_output_path.c_str();
        const int output_flags = child_output_path.empty()
            ? O_RDWR
            : (O_WRONLY | O_CREAT | O_APPEND);
        int output_fd = ::open(output_path, output_flags, 0644);
        if (output_fd < 0) {
            writeExecErrorAndExit(error_pipe[1], errno);
        }
        int input_fd = ::open("/dev/null", O_RDONLY);
        if (input_fd < 0) {
            writeExecErrorAndExit(error_pipe[1], errno);
        }
        if (::dup2(input_fd, STDIN_FILENO) < 0 ||
            ::dup2(output_fd, STDOUT_FILENO) < 0 ||
            ::dup2(output_fd, STDERR_FILENO) < 0) {
            writeExecErrorAndExit(error_pipe[1], errno);
        }
        if (input_fd > STDERR_FILENO) {
            ::close(input_fd);
        }
        if (output_fd > STDERR_FILENO) {
            ::close(output_fd);
        }
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) {
            writeExecErrorAndExit(error_pipe[1], errno);
        }
        ::execve(argv[0], argv.data(), envp.data());
        writeExecErrorAndExit(error_pipe[1], errno);
    }

    ::close(error_pipe[1]);
    int child_error = 0;
    ssize_t bytes_read = 0;
    do {
        bytes_read = ::read(error_pipe[0], &child_error, sizeof(child_error));
    } while (bytes_read < 0 && errno == EINTR);
    ::close(error_pipe[0]);

    if (bytes_read == static_cast<ssize_t>(sizeof(child_error))) {
        int status = 0;
        while (::waitpid(child_pid, &status, 0) < 0 && errno == EINTR) {
        }
        throw spawnError("execve", child_error);
    }
    if (bytes_read < 0) {
        const int error_code = errno;
        ::kill(-child_pid, SIGKILL);
        int status = 0;
        while (::waitpid(child_pid, &status, 0) < 0 && errno == EINTR) {
        }
        throw spawnError("read exec status", error_code);
    }
    pid_ = child_pid;
    common::Logger::instance().log(
        common::LogLevel::Info,
        "ProcessHandle",
        "started process pid=" + std::to_string(pid_) + ", argv=" + formatArgs(argv_storage));
}

void ProcessHandle::terminate() {
    if (pid_ <= 0) {
        return;
    }
    const int child_pid = pid_;
    common::Logger::instance().log(
        common::LogLevel::Info,
        "ProcessHandle",
        "terminating process pid=" + std::to_string(child_pid));
    ::kill(-child_pid, SIGTERM);
    ::kill(child_pid, SIGTERM);
    if (wait(3000) == -1 && pid_ > 0) {
        common::Logger::instance().log(
            common::LogLevel::Warn,
            "ProcessHandle",
            "process pid=" + std::to_string(child_pid) + " did not exit after SIGTERM; sending SIGKILL");
        ::kill(-child_pid, SIGKILL);
        ::kill(child_pid, SIGKILL);
        wait(1000);
    }
    pid_ = -1;
    common::Logger::instance().log(
        common::LogLevel::Info,
        "ProcessHandle",
        "terminated process pid=" + std::to_string(child_pid));
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
            const int finished_pid = pid_;
            pid_ = -1;
            common::Logger::instance().log(
                common::LogLevel::Info,
                "ProcessHandle",
                "process pid=" + std::to_string(finished_pid) + " exited status=" + std::to_string(status));
            return status;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                pid_ = -1;
                return 0;
            }
            return -1;
        }
        if (timeout_ms <= 0) {
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        waited += sleep_ms;
    }
    return -1;
}

void ProcessHandle::killByCmdlinePattern(const std::string& pattern) {
    const pid_t my_pid = ::getpid();
    DIR* proc_dir = ::opendir("/proc");
    if (!proc_dir) return;

    std::vector<pid_t> targets;

    struct dirent* entry = nullptr;
    while ((entry = ::readdir(proc_dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        const char* name = entry->d_name;
        // Only numeric names are pids.
        bool all_digits = true;
        for (const char* p = name; *p && all_digits; ++p) {
            all_digits = (*p >= '0' && *p <= '9');
        }
        if (!all_digits || name[0] == '\0') continue;

        pid_t pid = static_cast<pid_t>(std::stoi(name));
        if (pid <= 1 || pid == my_pid) continue;

        std::string cmdline_path = std::string("/proc/") + name + "/cmdline";
        std::ifstream cmdline_file(cmdline_path, std::ios::binary);
        if (!cmdline_file.is_open()) continue;

        std::string cmdline((std::istreambuf_iterator<char>(cmdline_file)),
                             std::istreambuf_iterator<char>());
        // Replace null bytes with spaces for readable matching.
        for (auto& c : cmdline) {
            if (c == '\0') c = ' ';
        }

        if (cmdline.find(pattern) != std::string::npos) {
            targets.push_back(pid);
        }
    }
    ::closedir(proc_dir);

    // Kill collected targets.
    if (targets.empty()) return;

    common::Logger::instance().log(
        common::LogLevel::Info,
        "ProcessHandle",
        "startup cleanup: found " + std::to_string(targets.size()) +
            " orphaned process(es) matching \"" + pattern + "\"");

    for (pid_t target : targets) {
        common::Logger::instance().log(
            common::LogLevel::Info,
            "ProcessHandle",
            "sending SIGTERM to orphaned process pid=" + std::to_string(target));
        // Try process group first, then individual.
        ::kill(-target, SIGTERM);
        ::kill(target, SIGTERM);
    }

    // Wait up to 3 seconds for them to exit.
    const int max_wait_ms = 3000;
    const int poll_ms = 50;
    for (int waited = 0; waited < max_wait_ms; waited += poll_ms) {
        bool any_alive = false;
        for (pid_t target : targets) {
            if (::kill(target, 0) == 0) {  // still alive
                any_alive = true;
                break;
            }
        }
        if (!any_alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }

    // Force-kill any survivors.
    for (pid_t target : targets) {
        if (::kill(target, 0) == 0) {
            common::Logger::instance().log(
                common::LogLevel::Warn,
                "ProcessHandle",
                "orphaned process pid=" + std::to_string(target) +
                    " still alive after SIGTERM; sending SIGKILL");
            ::kill(-target, SIGKILL);
            ::kill(target, SIGKILL);
        }
    }
}

}  // namespace recordlab::host
