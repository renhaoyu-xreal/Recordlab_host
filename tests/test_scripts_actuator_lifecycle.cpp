#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/scripts/scripts_actuator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (!qEnvironmentVariableIsSet("RECORDLAB_PYTHON_BIN")) {
        qputenv("RECORDLAB_PYTHON_BIN", QByteArray("python3"));
    }

    const fs::path tmp = fs::temp_directory_path() / ("recordlab_scripts_actuator_" + std::to_string(::getpid()));
    fs::create_directories(tmp);
    const fs::path script = tmp / "lifecycle_demo.py";
    {
        std::ofstream out(script);
        out << "print('script lifecycle hello', flush=True)\n";
        out << "import sys\n";
        out << "print('script lifecycle error', file=sys.stderr, flush=True)\n";
    }

    recordlab::host::HostMessageBus bus;
    bus.registerConsumer(recordlab::host::msg::UI);

    bool saw_started = false;
    bool saw_finished = false;
    bool saw_stdout = false;
    bool saw_stderr = false;
    int exit_code = -1;
    std::string script_id;

    {
        recordlab::host::ScriptsActuator actuator(
            bus,
            QString::fromStdString(tmp.string()),
            QString::fromStdString(tmp.string()),
            QString::fromStdString((tmp / "agents_config.json").string()));

        bus.publish({
            .source = recordlab::host::msg::UI,
            .target = recordlab::host::msg::SCRIPTS_ACTUATOR,
            .type = recordlab::host::msg::RUN_SCRIPT,
            .payload = {
                {"script_path", script.string()},
                {"agent_name", "imu_proxy"},
            },
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline && !saw_finished) {
            app.processEvents(QEventLoop::AllEvents, 50);
            for (const auto& msg : bus.drainFor(recordlab::host::msg::UI)) {
                if (msg.type == recordlab::host::msg::SCRIPT_STARTED) {
                    saw_started = true;
                    script_id = msg.payload.value("script_id", std::string{});
                    require(!script_id.empty(), "script_started should include script_id");
                    require(msg.payload.value("script_path", std::string{}) == script.string(),
                            "script_started should include resolved script_path");
                    require(msg.payload.value("agent_name", std::string{}) == "imu_proxy",
                            "script_started should include agent_name");
                    require(msg.payload.value("pid", 0) > 0, "script_started should include pid");
                } else if (msg.type == recordlab::host::msg::SCRIPT_OUTPUT) {
                    require(msg.payload.value("process", std::string{}) == "script",
                            "script_output should identify script process");
                    require(msg.payload.value("script_path", std::string{}) == script.string(),
                            "script_output should include script_path");
                    require(msg.payload.value("pid", 0) > 0, "script_output should include pid");
                    const auto output_script_id = msg.payload.value("script_id", std::string{});
                    require(!output_script_id.empty(), "script_output should include script_id");
                    if (!script_id.empty()) {
                        require(output_script_id == script_id, "script_output should include matching script_id");
                    }
                    const auto stream = msg.payload.value("stream", std::string{});
                    if (stream == "stdout") {
                        saw_stdout = true;
                    } else if (stream == "stderr") {
                        saw_stderr = true;
                    }
                } else if (msg.type == recordlab::host::msg::SCRIPT_FINISHED) {
                    saw_finished = true;
                    exit_code = msg.payload.value("exit_code", -1);
                    require(msg.payload.value("script_id", std::string{}) == script_id,
                            "script_finished should include matching script_id");
                    require(msg.payload.value("script_path", std::string{}) == script.string(),
                            "script_finished should include script_path");
                    require(msg.payload.value("pid", 0) > 0, "script_finished should include pid");
                }
            }
            QThread::msleep(10);
        }
    }

    fs::remove_all(tmp);
    require(saw_started, "script_started missing");
    require(saw_stdout, "script stdout missing");
    require(saw_stderr, "script stderr missing");
    require(saw_finished, "script_finished missing");
    require(exit_code == 0, "script should exit successfully");

    std::cout << "scripts actuator lifecycle ok\n";
    return 0;
}
