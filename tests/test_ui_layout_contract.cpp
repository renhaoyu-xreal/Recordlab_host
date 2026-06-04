#include "recordlab_host/ui/main_window.h"
#include "recordlab_host/ui/data_page.h"
#include "recordlab_host/ui/entry_page.h"
#include "recordlab_host/ui/script_page.h"
#include "recordlab_host/ui/sensor_workspace_widget.h"
#include "recordlab_host/ui/workspace_page.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <nlohmann/json.hpp>

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
    auto* script_output_tabs = workspace->scriptPage()->findChild<QTabWidget*>("script_output_tabs");
    require(script_output_tabs != nullptr, "script output tabs missing");
    require(script_output_tabs->tabText(0).startsWith(QStringLiteral("data输出目录：")), "script data tab should include output path");
    auto* command_data_group = workspace->dataPage()->findChild<QGroupBox*>("command_data_output_group");
    require(command_data_group != nullptr, "command data output group missing");
    require(command_data_group->title().startsWith(QStringLiteral("data输出目录：")), "command data group should include output path");

    auto* script_workspace = workspace->scriptPage()->sensorWorkspace();
    require(script_workspace->findChild<QGroupBox*>("data_selection_group") != nullptr, "data selection group missing");
    require(script_workspace->findChild<QGroupBox*>("custom_data_group") != nullptr, "custom data group missing");
    require(script_workspace->findChild<QGroupBox*>("realtime_values_group") != nullptr, "realtime values group missing");
    require(script_workspace->findChild<QLabel*>("motion_status_label") != nullptr, "motion status label missing");
    require(script_workspace->findChild<QWidget*>("video_panel_1") != nullptr, "video panel 1 missing");
    require(script_workspace->findChild<QWidget*>("video_panel_2") != nullptr, "video panel 2 missing");
    auto* curve_group = script_workspace->findChild<QGroupBox*>("curve_preview_group");
    require(curve_group != nullptr, "curve preview group missing");
    require(curve_group->title() == QStringLiteral("传感器数据曲线: 未选择数据"), "curve group should show empty selection");
    auto* curve_plot = script_workspace->findChild<QWidget*>("curve_plot_widget");
    require(curve_plot != nullptr, "curve plot widget missing");
    require(curve_plot->property("curve_panel_count").toInt() == 3, "curve plot should expose three RecordLabC-style panels");
    require(script_workspace->findChild<QLabel*>("selected_data_label") == nullptr, "selected data label should not occupy center space");
    require(script_workspace->dataSelectionList()->item(0)->text() == QStringLiteral("IMU0-gyro [--Hz]"), "imu0 gyro initial rate should be unknown");
    require(script_workspace->dataSelectionList()->item(3)->text() == QStringLiteral("IMU0-temperature [--Hz]"), "imu0 temperature initial rate should be unknown");
    require(script_workspace->dataSelectionList()->item(6)->text() == QStringLiteral("IMU1-temperature [--Hz]"), "imu1 temperature initial rate should be unknown");

    script_workspace->handleRealtimeData(QStringLiteral("imu_data"), nlohmann::json{
        {"type", 1},
        {"data", {1.0, 2.0, 3.0, 0.0, 0.0, 0.0}},
    }, 1000.0);
    require(script_workspace->dataSelectionList()->item(0)->text() == QStringLiteral("IMU0-gyro [1000Hz]"), "imu0 gyro live rate missing");
    require(!script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("Hz")), "realtime values should not display hz");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU0-gyro")), "realtime imu label missing");
    require(curve_group->title() == QStringLiteral("传感器数据曲线: 未选择数据"), "realtime data should not change selected data");

    script_workspace->dataSelectionList()->setCurrentRow(0);
    QApplication::processEvents();
    require(curve_group->title() == QStringLiteral("传感器数据曲线: IMU0-gyro"), "selected data should follow curve title");
    require(curve_plot->property("curve_sample_count").toInt() == 1, "selected imu curve should expose first sample");
    script_workspace->handleRealtimeData(QStringLiteral("imu_data"), nlohmann::json{
        {"type", 1},
        {"data", {2.0, 3.0, 4.0, 0.0, 0.0, 0.0}},
    }, 1000.0);
    QApplication::processEvents();
    require(curve_plot->property("curve_sample_count").toInt() == 2, "selected imu curve should update as data arrives");

    script_workspace->handleRealtimeData(QStringLiteral("camera_data"), nlohmann::json{
        {"timestamp", 1},
        {"cam_data", {
            {"0", {
                {"image", {
                    {"width", 2},
                    {"height", 2},
                    {"format", static_cast<int>(QImage::Format_Grayscale8)},
                    {"bytes_per_line", 2},
                    {"data", {{"__echo_bytes_base64__", "AQIDBA=="}}},
                }},
            }},
        }},
    }, 30.0);
    bool saw_camera_status = false;
    for (const auto* label : script_workspace->findChild<QWidget*>("video_panel_1")->findChildren<QLabel*>()) {
        saw_camera_status = saw_camera_status || label->text().contains(QStringLiteral("cam 0 | 2 x 2"));
    }
    require(saw_camera_status, "camera frame status missing");
    require(curve_group->title() == QStringLiteral("传感器数据曲线: IMU0-gyro"), "camera frames should not change selected data");

    auto* data_workspace = workspace->dataPage()->sensorWorkspace();
    require(data_workspace->findChild<QWidget*>("video_panel_1") != nullptr, "data page video panel missing");
    require(data_workspace->findChild<QWidget*>("curve_plot_widget") != nullptr, "data page curve plot missing");
    require(workspace->scriptPage()->scriptList() != nullptr, "script list missing");
    require(workspace->dataPage()->commandComboBox() != nullptr, "command combo box missing");
    require(workspace->dataPage()->commandComboBox()->findText(QStringLiteral("init_device")) >= 0, "init_device command option missing");
    return 0;
}
