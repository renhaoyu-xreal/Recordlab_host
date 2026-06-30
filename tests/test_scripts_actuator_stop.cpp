#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/scripts/scripts_actuator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

bool stepsContainStatus(const std::string& steps_json, const std::string& status) {
    if (steps_json.empty()) {
        return false;
    }
    try {
        const auto steps = nlohmann::json::parse(steps_json);
        if (!steps.is_array()) {
            return false;
        }
        for (const auto& step : steps) {
            if (step.is_object() && step.value("status", std::string{}) == status) {
                return true;
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (!qEnvironmentVariableIsSet("RECORDLAB_PYTHON_BIN")) {
        qputenv("RECORDLAB_PYTHON_BIN", QByteArray("python3"));
    }

    const fs::path exe_path = fs::weakly_canonical(fs::absolute(fs::path(argv[0])));
    const fs::path repo_root = exe_path.parent_path().parent_path();
    const fs::path nodes_root = repo_root / "third_party" / "Recordlab_nodes";
    const fs::path echo_root = repo_root / "third_party" / "echo_message_system" / "python";
    const fs::path agents_config = nodes_root / "config" / "agents_config.json";
    require(fs::exists(nodes_root / "node_scripts" / "runtime" / "run_recordlab_script.py"),
            "runtime script missing");
    require(fs::exists(agents_config), "agents_config.json missing");

    const fs::path tmp = fs::temp_directory_path() / ("recordlab_scripts_stop_" + std::to_string(::getpid()));
    fs::create_directories(tmp);
    const fs::path script = tmp / "stop_demo.py";
    {
        std::ofstream out(script);
        out << "all_agent_names = ['glasses_nviz_node', 'UR_node']\n";
        out << "from flowagent.core.script_workflow import WorkflowStep, set_steps, set_step\n";
        out << "import time\n";
        out << "set_steps([WorkflowStep.START_DEVICE, WorkflowStep.EXECUTE_TRAJECTORY], title='stop demo')\n";
        out << "set_step(WorkflowStep.EXECUTE_TRAJECTORY, 'running', '正在执行轨迹')\n";
        out << "while True:\n";
        out << "    time.sleep(0.2)\n";
    }

    recordlab::host::HostMessageBus bus;
    bus.registerConsumer(recordlab::host::msg::UI);
    bus.registerConsumer(recordlab::host::msg::AGENT_MANAGER);
    bus.registerConsumer(recordlab::host::msg::AGENT_MANAGER_PRIORITY);

    bool saw_started = false;
    bool saw_stopping = false;
    bool saw_stopped = false;
    bool saw_finished = false;
    bool saw_host_estop = false;
    bool stop_requested = false;
    int exit_code = -1;

    {
        recordlab::host::ScriptsActuator actuator(
            bus,
            QString::fromStdString(nodes_root.string()),
            QString::fromStdString(echo_root.string()),
            QString::fromStdString(agents_config.string()),
            QString::fromLocal8Bit(qgetenv("RECORDLAB_PYTHON_BIN").isEmpty()
                ? QByteArray("python3")
                : qgetenv("RECORDLAB_PYTHON_BIN")));

        bus.publish({
            .source = recordlab::host::msg::UI,
            .target = recordlab::host::msg::SCRIPTS_ACTUATOR,
            .type = recordlab::host::msg::RUN_SCRIPT,
            .payload = {
                {"script_path", script.string()},
                {"agent_name", "glasses_nviz_node"},
            },
        });

        bool stop_sent = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (std::chrono::steady_clock::now() < deadline && !saw_finished) {
            app.processEvents(QEventLoop::AllEvents, 50);

            for (const auto& msg : bus.drainFor(recordlab::host::msg::UI)) {
                if (msg.type == recordlab::host::msg::SCRIPT_STARTED) {
                    saw_started = true;
                } else if (msg.type == recordlab::host::msg::SCRIPT_WORKFLOW) {
                    if (msg.payload.value("action", std::string{}) == "clear") {
                        continue;
                    }
                    const auto steps_json = msg.payload.value("steps_json", std::string{});
                    saw_stopping = saw_stopping || stepsContainStatus(steps_json, "stopping");
                    saw_stopped = saw_stopped || stepsContainStatus(steps_json, "stopped");
                } else if (msg.type == recordlab::host::msg::SCRIPT_FINISHED) {
                    saw_finished = true;
                    stop_requested = msg.payload.value("stop_requested", false);
                    exit_code = msg.payload.value("exit_code", -1);
                }
            }

            for (const auto& msg : bus.drainFor(recordlab::host::msg::AGENT_MANAGER)) {
                if (msg.type != recordlab::host::msg::CMD_REQUEST) {
                    continue;
                }
                const auto agent_name = msg.payload.value("agent_name", std::string{});
                const auto cmd = msg.payload.value("cmd", std::string{});
                bus.publish({
                    .request_id = msg.request_id,
                    .source = recordlab::host::msg::AGENT_MANAGER,
                    .target = recordlab::host::msg::SCRIPTS_ACTUATOR,
                    .type = recordlab::host::msg::CMD_RESULT,
                    .payload = {
                        {"request_id", msg.request_id},
                        {"agent_name", agent_name},
                        {"cmd", cmd},
                        {"success", true},
                        {"message", "ok"},
                    },
                });
            }

            for (const auto& msg : bus.drainFor(recordlab::host::msg::AGENT_MANAGER_PRIORITY)) {
                if (msg.type != recordlab::host::msg::ESTOP) {
                    continue;
                }
                saw_host_estop = saw_host_estop
                    || msg.payload.value("agent_name", std::string{}) == "UR_node";
            }

            if (saw_started && !stop_sent) {
                bus.publish({
                    .source = recordlab::host::msg::UI,
                    .target = recordlab::host::msg::SCRIPTS_ACTUATOR,
                    .type = recordlab::host::msg::STOP_SCRIPT,
                });
                stop_sent = true;
            }

            QThread::msleep(10);
        }
    }

    fs::remove_all(tmp);
    require(saw_started, "script should start before stop");
    require(saw_stopping, "workflow should enter stopping state immediately");
    require(saw_stopped, "workflow should eventually enter stopped state");
    require(saw_host_estop, "host-side UR estop request missing");
    require(saw_finished, "script should finish after stop");
    require(stop_requested, "script_finished should report stop_requested");
    require(exit_code == 0 || exit_code == 130, "graceful stop should exit with code 0 or 130");

    long long destroyed_process_pid = 0;
    {
        recordlab::host::HostMessageBus bus;
        bus.registerConsumer(recordlab::host::msg::UI);
        bus.registerConsumer(recordlab::host::msg::AGENT_MANAGER);
        bus.registerConsumer(recordlab::host::msg::AGENT_MANAGER_PRIORITY);

        auto scope_tmp = fs::temp_directory_path() / ("recordlab_scripts_destroy_" + std::to_string(::getpid()));
        fs::create_directories(scope_tmp);
        const auto scope_script = scope_tmp / "destroy_demo.py";
        {
            std::ofstream out(scope_script);
            out << "import time\n";
            out << "while True:\n";
            out << "    time.sleep(0.2)\n";
        }

        {
            recordlab::host::ScriptsActuator actuator(
                bus,
                QString::fromStdString(nodes_root.string()),
                QString::fromStdString(echo_root.string()),
                QString::fromStdString(agents_config.string()),
                QString::fromLocal8Bit(qgetenv("RECORDLAB_PYTHON_BIN").isEmpty()
                    ? QByteArray("python3")
                    : qgetenv("RECORDLAB_PYTHON_BIN")));

            bus.publish({
                .source = recordlab::host::msg::UI,
                .target = recordlab::host::msg::SCRIPTS_ACTUATOR,
                .type = recordlab::host::msg::RUN_SCRIPT,
                .payload = {
                    {"script_path", scope_script.string()},
                    {"agent_name", "glasses_nviz_node"},
                },
            });

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (std::chrono::steady_clock::now() < deadline && destroyed_process_pid <= 0) {
                app.processEvents(QEventLoop::AllEvents, 50);
                for (const auto& msg : bus.drainFor(recordlab::host::msg::UI)) {
                    if (msg.type == recordlab::host::msg::SCRIPT_STARTED) {
                        destroyed_process_pid = msg.payload.value("pid", 0LL);
                    }
                }
                for (const auto& msg : bus.drainFor(recordlab::host::msg::AGENT_MANAGER)) {
                    if (msg.type != recordlab::host::msg::CMD_REQUEST) {
                        continue;
                    }
                    bus.publish({
                        .request_id = msg.request_id,
                        .source = recordlab::host::msg::AGENT_MANAGER,
                        .target = recordlab::host::msg::SCRIPTS_ACTUATOR,
                        .type = recordlab::host::msg::CMD_RESULT,
                        .payload = {
                            {"request_id", msg.request_id},
                            {"agent_name", msg.payload.value("agent_name", std::string{})},
                            {"cmd", msg.payload.value("cmd", std::string{})},
                            {"success", true},
                            {"message", "ok"},
                        },
                    });
                }
                QThread::msleep(10);
            }
        }

        require(destroyed_process_pid > 0, "destroy-time script should start");
        const auto kill_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < kill_deadline && ::kill(static_cast<pid_t>(destroyed_process_pid), 0) == 0) {
            QThread::msleep(50);
        }
        require(::kill(static_cast<pid_t>(destroyed_process_pid), 0) != 0,
                "ScriptsActuator destruction should not leave python process running");
        fs::remove_all(scope_tmp);
    }

    std::cout << "scripts actuator stop ok\n";
    return 0;
}
