#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "recordlab_host/common/process_handle.h"

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
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

std::runtime_error spawnError(const char* action, int error_code) {
    return std::runtime_error(std::string(action) + " failed: " + std::strerror(error_code));
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

    posix_spawn_file_actions_t file_actions;
    int rc = posix_spawn_file_actions_init(&file_actions);
    if (rc != 0) {
        throw spawnError("posix_spawn_file_actions_init", rc);
    }

    auto cleanup = [&file_actions]() {
        posix_spawn_file_actions_destroy(&file_actions);
    };
    auto add_action = [&](int error_code, const char* action) {
        if (error_code != 0) {
            cleanup();
            throw spawnError(action, error_code);
        }
    };

    add_action(posix_spawn_file_actions_addopen(
                   &file_actions, STDIN_FILENO, "/dev/null", O_RDWR, 0),
               "posix_spawn_file_actions_addopen");
    add_action(posix_spawn_file_actions_adddup2(
                   &file_actions, STDIN_FILENO, STDOUT_FILENO),
               "posix_spawn_file_actions_adddup2 stdout");
    add_action(posix_spawn_file_actions_adddup2(
                   &file_actions, STDIN_FILENO, STDERR_FILENO),
               "posix_spawn_file_actions_adddup2 stderr");
    if (!cwd.empty()) {
        add_action(posix_spawn_file_actions_addchdir_np(&file_actions, cwd.c_str()),
                   "posix_spawn_file_actions_addchdir_np");
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    std::vector<std::string> env_storage = buildEnvironment(pythonpath);
    std::vector<char*> envp;
    envp.reserve(env_storage.size() + 1);
    for (auto& entry : env_storage) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);

    pid_t child_pid = -1;
    rc = posix_spawnp(&child_pid, argv[0], &file_actions, nullptr, argv.data(), envp.data());
    cleanup();
    if (rc != 0) {
        throw spawnError("posix_spawnp", rc);
    }
    pid_ = child_pid;
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
