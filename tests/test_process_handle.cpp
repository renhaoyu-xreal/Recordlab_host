#include "recordlab_host/common/process_handle.h"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;

bool processAlive(int pid) {
    if (pid <= 0) return false;
    if (::kill(pid, 0) == 0) return true;
    return errno == EPERM;
}

bool waitForDead(int pid, int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!processAlive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return !processAlive(pid);
}

std::string readText(const fs::path& path) {
    std::ifstream in(path);
    std::string text;
    std::getline(in, text);
    return text;
}

int readPidFile(const fs::path& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream in(path);
        int pid = -1;
        if (in >> pid) return pid;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return -1;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / ("recordlab_process_handle_" + std::to_string(::getpid()));
    const fs::path cwd = tmp / "cwd";
    fs::create_directories(cwd);

    recordlab::host::ProcessHandle process;

    process.start({"/bin/sh", "-c",
                   "printf '%s\\n' \"$PYTHONPATH\" > env.txt; pwd > cwd.txt"},
                  cwd.string(), "recordlab:test:path");
    assert(process.wait(3000) != -1);
    assert(readText(cwd / "env.txt") == "recordlab:test:path");
    assert(readText(cwd / "cwd.txt") == cwd.string());

    std::mutex output_mutex;
    std::vector<nlohmann::json> outputs;
    process.start({"/bin/sh", "-c",
                   "printf 'node stdout\\n'; printf 'node stderr\\n' >&2"},
                  cwd.string(), "",
                  {
                      .process = "node",
                      .agent_name = "imu_proxy",
                      .node_name = "imu_proxy",
                  },
                  [&](nlohmann::json payload) {
                      std::lock_guard<std::mutex> lock(output_mutex);
                      outputs.push_back(std::move(payload));
                  });
    assert(process.wait(3000) != -1);
    bool saw_stdout = false;
    bool saw_stderr = false;
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        for (const auto& output : outputs) {
            assert(output.value("process", std::string{}) == "node");
            assert(output.value("agent_name", std::string{}) == "imu_proxy");
            assert(output.value("node_name", std::string{}) == "imu_proxy");
            assert(output.value("pid", 0) > 0);
            if (output.value("stream", std::string{}) == "stdout" &&
                output.value("text", std::string{}) == "node stdout") {
                saw_stdout = true;
            }
            if (output.value("stream", std::string{}) == "stderr" &&
                output.value("text", std::string{}) == "node stderr") {
                saw_stderr = true;
            }
        }
    }
    assert(saw_stdout);
    assert(saw_stderr);

    process.start({"/bin/sh", "-c", "sleep 30"}, cwd.string(), "");
    const int first_pid = process.pid();
    assert(processAlive(first_pid));
    process.start({"/bin/sh", "-c", "sleep 30"}, cwd.string(), "");
    const int second_pid = process.pid();
    assert(second_pid > 0);
    assert(second_pid != first_pid);
    assert(waitForDead(first_pid));
    process.terminate();
    assert(waitForDead(second_pid));

    const fs::path child_pid_path = tmp / "child.pid";
    process.start({"/bin/sh", "-c",
                   "sleep 30 & echo $! > \"$1\"; wait",
                   "recordlab-process-test", child_pid_path.string()},
                  cwd.string(), "");
    const int shell_pid = process.pid();
    const int child_pid = readPidFile(child_pid_path);
    assert(processAlive(shell_pid));
    assert(processAlive(child_pid));
    process.terminate();
    assert(waitForDead(shell_pid));
    assert(waitForDead(child_pid));

    fs::remove_all(tmp);
    std::cout << "process handle ok\n";
    return 0;
}
