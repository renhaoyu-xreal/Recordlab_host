#include "recordlab_host/ui/main_window.h"
#include "recordlab_host/ui/data_page.h"
#include "recordlab_host/ui/entry_page.h"
#include "recordlab_host/ui/script_page.h"
#include "recordlab_host/ui/sensor_workspace_widget.h"
#include "recordlab_host/ui/workspace_page.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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

    const std::string config_path = "/tmp/recordlab_ui_agents_config.json";
    std::ofstream config(config_path);
    config << R"({
      "agents": {
        "imu_simulation": {
          "name": "imu_simulation",
          "node_class": "recordlab_nodes.nodes.imu_sim.imu_sim_node.ImuSimNode",
          "process_type": "python_node",
          "subnode_host": "127.0.0.1",
          "action_name": "imu_simulation_actions",
          "goal_port": 5690,
          "feedback_port": 5691,
          "data_port": 16510,
          "root_path": "data",
          "topics": []
        }
      },
      "primary_agents": ["imu_simulation"]
    })";
    config.close();

    recordlab::host::ui::MainWindow window(config_path, ".", ".");
    auto* entry = window.entryPage();
    require(entry != nullptr, "entry page missing");
    require(entry->findChild<QPushButton*>("agent_button_imu_simulation") != nullptr, "agent button missing");
    require(!entry->summaryLabel()->isVisible(), "entry page should not show version/info summary");

    auto* workspace = window.workspacePage();
    require(workspace != nullptr, "workspace page missing");
    auto* tabs = workspace->tabWidget();
    require(tabs != nullptr, "workspace tabs missing");
    require(tabs->count() == 2, "workspace should expose exactly two tabs");
    require(tabs->tabText(0) == QStringLiteral("脚本执行"), "first tab should be script execution");
    require(tabs->tabText(1) == QStringLiteral("数据 + 命令"), "second tab should be data + command");

    auto* script_workspace = workspace->scriptPage()->sensorWorkspace();
    require(script_workspace->findChild<QGroupBox*>("data_selection_group") != nullptr, "data selection group missing");
    require(script_workspace->findChild<QGroupBox*>("custom_data_group") != nullptr, "custom data group missing");
    require(script_workspace->findChild<QGroupBox*>("realtime_values_group") != nullptr, "realtime values group missing");
    require(script_workspace->findChild<QLabel*>("motion_status_label") != nullptr, "motion status label missing");
    require(script_workspace->findChild<QWidget*>("video_panel_1") != nullptr, "video panel 1 missing");
    require(script_workspace->findChild<QWidget*>("video_panel_2") != nullptr, "video panel 2 missing");
    require(script_workspace->findChild<QWidget*>("curve_panel_1") != nullptr, "curve panel 1 missing");
    require(script_workspace->findChild<QWidget*>("curve_panel_2") != nullptr, "curve panel 2 missing");
    require(script_workspace->findChild<QWidget*>("curve_panel_3") != nullptr, "curve panel 3 missing");

    auto* data_workspace = workspace->dataPage()->sensorWorkspace();
    require(data_workspace->findChild<QWidget*>("video_panel_1") != nullptr, "data page video panel missing");
    require(data_workspace->findChild<QWidget*>("curve_panel_3") != nullptr, "data page curve panel missing");
    require(workspace->scriptPage()->scriptList() != nullptr, "script list missing");
    require(workspace->dataPage()->commandComboBox() != nullptr, "command combo box missing");
    require(workspace->dataPage()->commandComboBox()->findText(QStringLiteral("init_device")) >= 0, "init_device command option missing");
    return 0;
}
