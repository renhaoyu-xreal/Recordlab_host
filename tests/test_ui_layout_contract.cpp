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
#include <QSplitter>
#include <QTabWidget>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

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

int freePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);
    close(fd);
    return port;
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
          "root_path": "data"
        }
      },
      "primary_agents": ["imu_simulation"]
    })";
    config.close();

    recordlab::host::ui::MainWindow window(config_path, ".", ".", {}, {}, {}, "127.0.0.1", freePort());
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
    require(workspace->scriptPage()->findChild<QPushButton*>("import_script_button") != nullptr,
            "script import button missing");
    require(workspace->scriptPage()->findChild<QPushButton*>("refresh_scripts_button") == nullptr,
            "refresh scripts button should be removed");
    require(workspace->scriptPage()->findChild<QPushButton*>("clear_scripts_button") == nullptr,
            "clear scripts button should be removed");
    auto* script_bottom_splitter = workspace->scriptPage()->findChild<QSplitter*>("script_bottom_splitter");
    require(script_bottom_splitter != nullptr, "script bottom splitter missing");
    const auto script_sizes = script_bottom_splitter->sizes();
    require(script_sizes.size() == 2 && script_sizes[0] > 0 && script_sizes[1] > 0,
            "script bottom splitter should expose two visible panes");
    require(script_sizes[0] * 5 <= (script_sizes[0] + script_sizes[1]) * 3,
            "script log should default to no more than 2/5 width");
    auto* command_data_group = workspace->dataPage()->findChild<QGroupBox*>("command_data_output_group");
    require(command_data_group != nullptr, "command data output group missing");
    require(command_data_group->title().startsWith(QStringLiteral("data输出目录：")), "command data group should include output path");

    auto* script_workspace = workspace->scriptPage()->sensorWorkspace();
    workspace->configureSensorLayout(nlohmann::json{
        {"imu_data", {
            {"channels", {
                {{"type", 1}, {"label", "IMU0-gyro"}},
                {{"type", 2}, {"label", "IMU0-acc"}},
                {{"type", 3}, {"label", "IMU0-mag"}},
                {{"type", 12}, {"label", "IMU0-temperature"}},
                {{"type", 4}, {"label", "IMU1-gyro"}},
                {{"type", 5}, {"label", "IMU1-acc"}},
                {{"type", 13}, {"label", "IMU1-temperature"}},
            }},
        }},
        {"motion_status", {{"display_name", "motion_status"}, {"ui_widget", "label"}}},
        {"camera_data", {{"display_name", "camera_data"}, {"ui_widget", "camera_preview"}}},
    });
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
    require(script_workspace->realtimeValueView()->toPlainText().split('\n').size() == script_workspace->dataSelectionList()->count(),
            "realtime values should mirror data selection count");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU1-temperature")),
            "realtime values should include every imu row initially");

    script_workspace->handleRealtimeData(QStringLiteral("imu_data"), nlohmann::json{
        {"type", 1},
        {"data", {1.0, 2.0, 3.0, 0.0, 0.0, 0.0}},
    }, 1000.0);
    require(script_workspace->dataSelectionList()->item(0)->text() == QStringLiteral("IMU0-gyro [1000Hz]"), "imu0 gyro live rate missing");
    require(!script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("Hz")), "realtime values should not display hz");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU0-gyro")), "realtime imu label missing");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU0-acc x:-- y:-- z:--")),
            "realtime update should not remove other imu rows");
    require(curve_group->title() == QStringLiteral("传感器数据曲线: 未选择数据"), "realtime data should not change selected data");

    script_workspace->handleRealtimeData(QStringLiteral("imu_data"), nlohmann::json{
        {"type", 2},
        {"data", {4.0, 5.0, 6.0, 0.0, 0.0, 0.0}},
    }, 500.0);
    require(script_workspace->dataSelectionList()->item(1)->text() == QStringLiteral("IMU0-acc [500Hz]"), "imu0 acc live rate missing");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU0-gyro type:1 x:1.000 y:2.000 z:3.000")),
            "realtime values should retain gyro latest value");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU0-acc type:2 x:4.000 y:5.000 z:6.000")),
            "realtime values should show acc latest value");

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

    workspace->configureSensorLayout(nlohmann::json{
        {"android_imu_data", {
            {"channels", {
                {{"type", 1}, {"label", "gyro"}},
                {{"type", 2}, {"label", "acc"}},
                {{"type", 3}, {"label", "mag"}},
                {{"type", 12}, {"label", "temperature"}},
            }},
        }},
        {"record_timer", {{"display_name", "record_timer"}, {"ui_widget", "value"}}},
    });
    require(script_workspace->dataSelectionList()->count() == 4,
            "android channel rows should appear in data selection");
    require(script_workspace->dataSelectionList()->item(0)->text() == QStringLiteral("gyro [--Hz]"),
            "android gyro row missing");
    require(script_workspace->dataSelectionList()->item(1)->text() == QStringLiteral("acc [--Hz]"),
            "android acc row missing");
    require(script_workspace->dataSelectionList()->item(2)->text() == QStringLiteral("mag [--Hz]"),
            "android mag row missing");
    require(script_workspace->dataSelectionList()->item(3)->text() == QStringLiteral("temperature [--Hz]"),
            "android temperature row missing");
    script_workspace->handleRealtimeData(QStringLiteral("android_imu_data"), nlohmann::json{
        {"type", 2},
        {"data", {7.0, 8.0, 9.0, 0.0, 0.0, 0.0}},
    }, 100.0);
    require(script_workspace->dataSelectionList()->item(1)->text() == QStringLiteral("acc [100Hz]"),
            "android acc live rate missing");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("acc type:2 x:7.000 y:8.000 z:9.000")),
            "android acc realtime value missing");
    script_workspace->dataSelectionList()->setCurrentRow(1);
    QApplication::processEvents();
    require(curve_group->title() == QStringLiteral("传感器数据曲线: acc"),
            "android selected data should follow curve title");
    require(curve_plot->property("curve_sample_count").toInt() == 1,
            "android selected curve should expose sample");
    script_workspace->handleRealtimeData(QStringLiteral("android_imu_data"), nlohmann::json{
        {"type", 12},
        {"data", {36.5, 0.0, 0.0, 0.0, 0.0, 0.0}},
    }, 1.0);
    require(script_workspace->dataSelectionList()->item(3)->text() == QStringLiteral("temperature [1.0Hz]"),
            "android temperature live rate missing");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("temperature type:12 temperature:36.50")),
            "android temperature realtime value missing");
    script_workspace->dataSelectionList()->setCurrentRow(3);
    QApplication::processEvents();
    require(curve_group->title() == QStringLiteral("传感器数据曲线: temperature"),
            "android temperature selected data should follow curve title");
    require(curve_plot->property("curve_sample_count").toInt() == 1,
            "android temperature curve should expose sample");

    auto* data_workspace = workspace->dataPage()->sensorWorkspace();
    require(data_workspace->findChild<QWidget*>("video_panel_1") != nullptr, "data page video panel missing");
    require(data_workspace->findChild<QWidget*>("curve_plot_widget") != nullptr, "data page curve plot missing");
    require(workspace->scriptPage()->scriptList() != nullptr, "script list missing");
    require(workspace->dataPage()->commandComboBox() != nullptr, "command combo box missing");
    require(workspace->dataPage()->commandComboBox()->findText(QStringLiteral("init_device")) >= 0, "init_device command option missing");
    return 0;
}
