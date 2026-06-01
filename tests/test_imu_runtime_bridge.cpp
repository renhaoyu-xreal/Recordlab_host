#include "recordlab_host/ui/imu_runtime_bridge.h"

#include <QApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void ensureApp(int& argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
    static QApplication app(argc, argv);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    ensureApp(argc, argv);

    const auto cwd = std::filesystem::current_path();
    const auto host_root = cwd.filename() == "build" ? cwd.parent_path() : cwd;
    const char* nodes_env = std::getenv("RECORDLAB_NODES_ROOT");
    const std::filesystem::path nodes_root = nodes_env
        ? std::filesystem::path(nodes_env)
        : host_root / "third_party" / "Recordlab_nodes";
    const std::string config_path = "/tmp/recordlab_imu_runtime_bridge_config.json";
    std::ofstream config(config_path);
    config << R"({
      "agents": {
        "imu_test": {
          "name": "imu_test",
          "node_class": "recordlab_nodes.nodes.imu_sim.imu_sim_node.ImuSimNode",
          "process_type": "python_node",
          "subnode_host": "127.0.0.1",
          "action_name": "imu_test_actions",
          "goal_port": 25690,
          "feedback_port": 25691,
          "root_path": "/tmp/recordlab_imu_runtime_bridge_data",
          "init_device_params": {
            "read_path": ")" << (nodes_root / "data" / "samples" / "imu_0.csv").string() << R"("
          },
          "default_scripts": ["imu_simulation_record_demo.py"],
          "topics": [
            {"name": "imu_data", "port": 26510, "encoding": "json"},
            {"name": "record_timer", "port": 26520, "encoding": "json"},
            {"name": "time_delay", "port": 26521, "encoding": "json"},
            {"name": "motion_status", "port": 26525, "encoding": "json"}
          ],
          "custom_params": {}
        }
      },
      "primary_agents": ["imu_test"]
    })";
    config.close();

    recordlab::host::ui::ImuRuntimeBridge bridge(config_path);
    bool healthy = false;
    bool got_runtime_data = false;
    QObject::connect(&bridge, &recordlab::host::ui::ImuRuntimeBridge::watchdogStateChanged,
                     [&](const QString& state) { healthy = state == QStringLiteral("HEALTHY"); });

    QEventLoop loop;
    QObject::connect(&bridge, &recordlab::host::ui::ImuRuntimeBridge::topicDataReceived, &bridge,
                     [&](const QString& name, const QString&, double) {
                         if (name == QStringLiteral("imu_data") || name == QStringLiteral("time_delay")
                             || name == QStringLiteral("motion_status")) {
                             got_runtime_data = true;
                             loop.quit();
                         }
                     },
                     Qt::QueuedConnection);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    bridge.activateAgent(QStringLiteral("imu_test"));
    require(healthy, "imu runtime bridge did not become healthy");
    bridge.sendCommand(QStringLiteral("start_device"), QStringLiteral("{}"));
    loop.exec();
    bridge.sendCommand(QStringLiteral("stop_device"), QStringLiteral("{}"));
    bridge.shutdown();

    require(got_runtime_data, "imu runtime bridge did not receive runtime topic data");
    return 0;
}
