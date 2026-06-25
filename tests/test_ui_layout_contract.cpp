#include <any>
#include <sstream>

#define private public
#include "recordlab_host/ui/main_window.h"
#undef private
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/ui/data_page.h"
#include "recordlab_host/ui/entry_page.h"
#include "recordlab_host/ui/script_page.h"
#include "recordlab_host/ui/sensor_workspace_widget.h"
#include "recordlab_host/ui/workspace_page.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
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

struct ScopedCameraShm {
    std::string name;
    int fd = -1;
    void* mapping = nullptr;
    std::size_t size = 0;

    ~ScopedCameraShm() {
        if (mapping && mapping != MAP_FAILED) {
            munmap(mapping, size);
        }
        if (fd >= 0) {
            close(fd);
        }
        if (!name.empty()) {
            shm_unlink(name.c_str());
        }
    }
};

ScopedCameraShm createCameraShm() {
    constexpr std::uint32_t kHeaderMagic = 0x52434d48;  // RCMH
    constexpr int kHeaderSize = 64;
    constexpr int kCameraCount = 2;
    constexpr int kSlotCount = 4;
    constexpr int kMetaSize = 64;
    constexpr int kSlotSize = 1024;
    constexpr std::size_t kSeqSize = kCameraCount * kSlotCount * sizeof(std::uint64_t);
    constexpr std::size_t kTotalSize = kHeaderSize + kSeqSize + kCameraCount * kSlotCount * kSlotSize;
    const std::string shm_name = "/recordlab_test_camera_shm_ui";
    shm_unlink(shm_name.c_str());

    ScopedCameraShm shm;
    shm.name = shm_name;
    shm.size = kTotalSize;
    shm.fd = shm_open(shm.name.c_str(), O_CREAT | O_RDWR, 0600);
    require(shm.fd >= 0, "failed to create camera shm");
    require(ftruncate(shm.fd, static_cast<off_t>(shm.size)) == 0, "failed to resize camera shm");
    shm.mapping = mmap(nullptr, shm.size, PROT_READ | PROT_WRITE, MAP_SHARED, shm.fd, 0);
    require(shm.mapping != MAP_FAILED, "failed to map camera shm");
    std::memset(shm.mapping, 0, shm.size);

    auto* bytes = static_cast<unsigned char*>(shm.mapping);
    auto write_u32 = [&](std::size_t offset, std::uint32_t value) {
        std::memcpy(bytes + offset, &value, sizeof(value));
    };
    auto write_u64 = [&](std::size_t offset, std::uint64_t value) {
        std::memcpy(bytes + offset, &value, sizeof(value));
    };

    write_u32(0, kHeaderMagic);
    write_u32(4, 1);
    write_u32(8, kCameraCount);
    write_u32(12, kSlotCount);
    write_u32(16, kSlotSize);
    write_u32(20, kMetaSize);

    const std::size_t seq_offset = kHeaderSize;
    const std::size_t slots_offset = kHeaderSize + kSeqSize;
    write_u64(seq_offset, 1);

    const std::size_t slot_offset = slots_offset;
    write_u32(slot_offset + 0, 2);
    write_u32(slot_offset + 4, 2);
    write_u32(slot_offset + 8, static_cast<std::uint32_t>(QImage::Format_Grayscale8));
    write_u32(slot_offset + 12, 4);
    write_u32(slot_offset + 16, 2);
    write_u32(slot_offset + 20, 0);
    bytes[slot_offset + kMetaSize + 0] = 1;
    bytes[slot_offset + kMetaSize + 1] = 2;
    bytes[slot_offset + kMetaSize + 2] = 3;
    bytes[slot_offset + kMetaSize + 3] = 4;
    return shm;
}

ScopedCameraShm createJpegCameraShm(std::uint64_t seq = 2) {
    constexpr std::uint32_t kHeaderMagic = 0x52434d48;  // RCMH
    constexpr int kHeaderSize = 64;
    constexpr int kCameraCount = 2;
    constexpr int kSlotCount = 4;
    constexpr int kMetaSize = 64;
    constexpr int kSlotSize = 4096;
    constexpr std::size_t kSeqSize = kCameraCount * kSlotCount * sizeof(std::uint64_t);
    constexpr std::size_t kTotalSize = kHeaderSize + kSeqSize + kCameraCount * kSlotCount * kSlotSize;
    const std::string shm_name = "/recordlab_test_camera_shm_ui_jpeg";
    shm_unlink(shm_name.c_str());

    QImage image(2, 2, QImage::Format_RGB888);
    image.fill(QColor(255, 64, 32));
    QByteArray jpegBytes;
    QBuffer jpegBuffer(&jpegBytes);
    require(jpegBuffer.open(QIODevice::WriteOnly), "failed to open jpeg buffer");
    require(image.save(&jpegBuffer, "JPEG", 72), "failed to encode jpeg preview");
    jpegBuffer.close();

    ScopedCameraShm shm;
    shm.name = shm_name;
    shm.size = kTotalSize;
    shm.fd = shm_open(shm.name.c_str(), O_CREAT | O_RDWR, 0600);
    require(shm.fd >= 0, "failed to create jpeg camera shm");
    require(ftruncate(shm.fd, static_cast<off_t>(shm.size)) == 0, "failed to resize jpeg camera shm");
    shm.mapping = mmap(nullptr, shm.size, PROT_READ | PROT_WRITE, MAP_SHARED, shm.fd, 0);
    require(shm.mapping != MAP_FAILED, "failed to map jpeg camera shm");
    std::memset(shm.mapping, 0, shm.size);

    auto* bytes = static_cast<unsigned char*>(shm.mapping);
    auto write_u32 = [&](std::size_t offset, std::uint32_t value) {
        std::memcpy(bytes + offset, &value, sizeof(value));
    };
    auto write_u64 = [&](std::size_t offset, std::uint64_t value) {
        std::memcpy(bytes + offset, &value, sizeof(value));
    };

    write_u32(0, kHeaderMagic);
    write_u32(4, 1);
    write_u32(8, kCameraCount);
    write_u32(12, kSlotCount);
    write_u32(16, kSlotSize);
    write_u32(20, kMetaSize);

    const std::size_t seq_offset = kHeaderSize;
    const std::size_t slots_offset = kHeaderSize + kSeqSize;
    const std::size_t slot_index = static_cast<std::size_t>(seq % kSlotCount);
    write_u64(seq_offset + slot_index * sizeof(std::uint64_t), seq);

    const std::size_t slot_offset = slots_offset + slot_index * kSlotSize;
    write_u32(slot_offset + 0, 2);
    write_u32(slot_offset + 4, 2);
    write_u32(slot_offset + 8, static_cast<std::uint32_t>(QImage::Format_RGB888));
    write_u32(slot_offset + 12, static_cast<std::uint32_t>(jpegBytes.size()));
    write_u32(slot_offset + 16, 6);
    write_u32(slot_offset + 20, 1);
    std::memcpy(bytes + slot_offset + kMetaSize, jpegBytes.constData(), static_cast<std::size_t>(jpegBytes.size()));
    return shm;
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
    auto* version_status = window.findChild<QLabel*>("app_version_status_label");
    require(version_status != nullptr, "version status label missing");
    require(version_status->text() == QStringLiteral("版本：v1.0.0"), "version status should use default fallback");
    require(window.statusBar()->currentMessage() == QStringLiteral("主 Agent：未选择"),
            "active agent text should live in the left status bar message area");

    auto* workspace = window.workspacePage();
    require(workspace != nullptr, "workspace page missing");
    auto* timer_value_label = workspace->findChild<QLabel*>("record_timer_value_label");
    auto* delay_value_label = workspace->findChild<QLabel*>("time_delay_value_label");
    require(timer_value_label != nullptr, "record timer value label missing");
    require(delay_value_label != nullptr, "time delay value label missing");
    require(timer_value_label->text() == QStringLiteral("00:00.000"), "record timer should match legacy default format");
    require(delay_value_label->text() == QStringLiteral("0.000 ms"), "time delay should match legacy default format");

    window.activateAgent(QStringLiteral("imu_simulation"));
    QApplication::processEvents();
    require(window.statusBar()->currentMessage() == QStringLiteral("主 Agent：imu_simulation"),
            "active agent status should track selected agent");

    window.recordTimerChanged(61.234);
    window.timeDelayChanged(12.5);
    QApplication::processEvents();
    require(timer_value_label->text() == QStringLiteral("01:01.234"), "record timer should match legacy MM:SS.mmm format");
    require(delay_value_label->text() == QStringLiteral("12.500 ms"), "time delay should match legacy millisecond format");

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
    require(script_workspace->dataSelectionList()->item(0)->text() == QStringLiteral("✗ IMU0-gyro [--Hz]"), "imu0 gyro initial rate should be unknown");
    require(script_workspace->dataSelectionList()->item(3)->text() == QStringLiteral("✗ IMU0-temperature [--Hz]"), "imu0 temperature initial rate should be unknown");
    require(script_workspace->dataSelectionList()->item(6)->text() == QStringLiteral("✗ IMU1-temperature [--Hz]"), "imu1 temperature initial rate should be unknown");
    require(script_workspace->realtimeValueView()->toPlainText().split('\n').size() == script_workspace->dataSelectionList()->count(),
            "realtime values should mirror data selection count");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU1-temperature")),
            "realtime values should include every imu row initially");

    script_workspace->handleRealtimeData(QStringLiteral("imu_data"), nlohmann::json{
        {"type", 1},
        {"data", {1.0, 2.0, 3.0, 0.0, 0.0, 0.0}},
    }, 1000.0);
    require(script_workspace->dataSelectionList()->item(0)->text() == QStringLiteral("✓ IMU0-gyro [1000Hz]"), "imu0 gyro live rate missing");
    require(!script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("Hz")), "realtime values should not display hz");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU0-gyro")), "realtime imu label missing");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("IMU0-acc x:-- y:-- z:--")),
            "realtime update should not remove other imu rows");
    require(curve_group->title() == QStringLiteral("传感器数据曲线: 未选择数据"), "realtime data should not change selected data");

    script_workspace->handleRealtimeData(QStringLiteral("imu_data"), nlohmann::json{
        {"type", 2},
        {"data", {4.0, 5.0, 6.0, 0.0, 0.0, 0.0}},
    }, 500.0);
    require(script_workspace->dataSelectionList()->item(1)->text() == QStringLiteral("✓ IMU0-acc [500Hz]"), "imu0 acc live rate missing");
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

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    auto shm = createCameraShm();
    script_workspace->handleRealtimeData(QStringLiteral("camera_data"), nlohmann::json{
        {"timestamp", 2},
        {"cam_data", {
            {"0", {
                {"image", {
                    {"width", 2},
                    {"height", 2},
                    {"encoding", "shm_raw"},
                    {"shm", true},
                    {"shm_name", shm.name.substr(1)},
                    {"shm_seq", 1},
                }},
            }},
        }},
    }, 30.0);
    QApplication::processEvents();
    bool saw_shm_camera_status = false;
    QStringList shm_camera_statuses;
    for (const auto* label : script_workspace->findChild<QWidget*>("video_panel_1")->findChildren<QLabel*>()) {
        shm_camera_statuses << label->text();
        saw_shm_camera_status = saw_shm_camera_status || label->text().contains(QStringLiteral("cam 0 | 2 x 2 | shm seq 1"));
    }
    require(
        saw_shm_camera_status,
        ("shared-memory camera preview status missing: " + shm_camera_statuses.join(" | ").toStdString()));
    require(curve_group->title() == QStringLiteral("传感器数据曲线: IMU0-gyro"), "shared-memory camera frames should not change selected data");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    auto jpeg_shm = createJpegCameraShm();
    script_workspace->handleRealtimeData(QStringLiteral("camera_data"), nlohmann::json{
        {"timestamp", 3},
        {"cam_data", {
            {"0", {
                {"image", {
                    {"width", 2},
                    {"height", 2},
                    {"encoding", "shm_jpeg"},
                    {"encoded_format", "JPEG"},
                    {"shm", true},
                    {"shm_name", jpeg_shm.name.substr(1)},
                    {"shm_seq", 2},
                }},
            }},
        }},
    }, 30.0);
    QApplication::processEvents();
    bool saw_jpeg_shm_status = false;
    QStringList jpeg_shm_statuses;
    for (const auto* label : script_workspace->findChild<QWidget*>("video_panel_1")->findChildren<QLabel*>()) {
        jpeg_shm_statuses << label->text();
        saw_jpeg_shm_status = saw_jpeg_shm_status || label->text().contains(QStringLiteral("cam 0 | 2 x 2 | shm seq 2"));
    }
    require(
        saw_jpeg_shm_status,
        ("shared-memory jpeg camera preview status missing: " + jpeg_shm_statuses.join(" | ").toStdString()));
    require(curve_group->title() == QStringLiteral("传感器数据曲线: IMU0-gyro"), "shared-memory jpeg camera frames should not change selected data");

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
    require(script_workspace->dataSelectionList()->item(0)->text() == QStringLiteral("✗ gyro [--Hz]"),
            "android gyro row missing");
    require(script_workspace->dataSelectionList()->item(1)->text() == QStringLiteral("✗ acc [--Hz]"),
            "android acc row missing");
    require(script_workspace->dataSelectionList()->item(2)->text() == QStringLiteral("✗ mag [--Hz]"),
            "android mag row missing");
    require(script_workspace->dataSelectionList()->item(3)->text() == QStringLiteral("✗ temperature [--Hz]"),
            "android temperature row missing");
    script_workspace->handleRealtimeData(QStringLiteral("android_imu_data"), nlohmann::json{
        {"type", 2},
        {"data", {7.0, 8.0, 9.0, 0.0, 0.0, 0.0}},
    }, 100.0);
    require(script_workspace->dataSelectionList()->item(1)->text() == QStringLiteral("✓ acc [100Hz]"),
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
        {"type", 2},
        {"data", {17.0, 18.0, 19.0, 0.0, 0.0, 0.0}},
    }, 100.0);
    QApplication::processEvents();
    const double smoothed_acc_x = curve_plot->property("curve_latest_x").toDouble();
    require(smoothed_acc_x > 7.0 && smoothed_acc_x < 17.0,
            "android acc curve should store smoothed display data, not raw jump");
    script_workspace->handleRealtimeData(QStringLiteral("android_imu_data"), nlohmann::json{
        {"type", 12},
        {"data", {36.5, 0.0, 0.0, 0.0, 0.0, 0.0}},
    }, 1.0);
    require(script_workspace->dataSelectionList()->item(3)->text() == QStringLiteral("✓ temperature [1.0Hz]"),
            "android temperature live rate missing");
    require(script_workspace->realtimeValueView()->toPlainText().contains(QStringLiteral("temperature type:12 temperature:36.50")),
            "android temperature realtime value missing");
    script_workspace->dataSelectionList()->setCurrentRow(3);
    QApplication::processEvents();
    require(curve_group->title() == QStringLiteral("传感器数据曲线: temperature"),
            "android temperature selected data should follow curve title");
    require(curve_plot->property("curve_sample_count").toInt() == 1,
            "android temperature curve should expose sample");

    workspace->configureSensorLayout(nlohmann::json{
        {"nebula_latest_csv", {
            {"display_name", "Nebula 最新数据"},
            {"ui_widget", "summary_value"},
            {"poll_interval_ms", 1000}
        }},
    });
    require(script_workspace->customDataList()->count() == 1,
            "nebula summary row should appear in custom data list");
    require(script_workspace->customDataList()->item(0)->text() == QStringLiteral("Nebula 最新数据"),
            "nebula summary row text mismatch");
    script_workspace->handleSummaryData(QStringLiteral("Nebula 最新数据"), nlohmann::json{
        {"latest_csv_lines", {
            {"air_data.csv", "air_last_row"},
            {"mobile_data.csv", "mobile_last_row"},
        }},
        {"latest_update_time", "12:34:56"},
    });
    const QString nebula_text = script_workspace->realtimeValueView()->toPlainText();
    require(nebula_text.contains(QStringLiteral("Nebula 最新数据")),
            "nebula summary title missing in realtime panel");
    require(nebula_text.contains(QStringLiteral("air_data.csv: air_last_row")),
            "nebula air latest row missing");
    require(nebula_text.contains(QStringLiteral("mobile_data.csv: mobile_last_row")),
            "nebula mobile latest row missing");
    require(nebula_text.contains(QStringLiteral("更新时间: 12:34:56")),
            "nebula summary update time missing");
    script_workspace->customDataList()->setCurrentRow(0);
    QApplication::processEvents();
    require(curve_group->title() == QStringLiteral("传感器数据曲线: Nebula 最新数据"),
            "nebula selected data should follow curve title");
    require(curve_plot->property("curve_sample_count").toInt() == 0,
            "nebula summary should not create curve samples");

    auto* data_workspace = workspace->dataPage()->sensorWorkspace();
    require(data_workspace->findChild<QWidget*>("video_panel_1") != nullptr, "data page video panel missing");
    require(data_workspace->findChild<QWidget*>("curve_plot_widget") != nullptr, "data page curve plot missing");
    require(workspace->scriptPage()->scriptList() != nullptr, "script list missing");
    require(workspace->dataPage()->commandComboBox() != nullptr, "command combo box missing");
    require(workspace->dataPage()->commandComboBox()->findText(QStringLiteral("init_device")) >= 0, "init_device command option missing");

    QTimer::singleShot(0, [&window, workspace]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog != nullptr, "live cookie dialog missing");

        QLabel* info_label = nullptr;
        for (auto* label : dialog->findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("当前 Agent："))) {
                info_label = label;
                break;
            }
        }
        require(info_label != nullptr, "live cookie info label missing");
        require(info_label->text().contains(QStringLiteral("还在获取fsn，请等待")),
                "dialog should show fsn waiting hint before cookies arrive");

        recordlab::host::HostMessage cookie_message;
        cookie_message.source = "data_receiver";
        cookie_message.target = recordlab::host::msg::UI;
        cookie_message.type = recordlab::host::msg::NODE_COOKIES;
        cookie_message.payload = {
            {"cookies", nlohmann::json::array({
                {
                    {"key", "product_id"},
                    {"value", "1082"},
                    {"is_display", true},
                },
                {
                    {"key", "name"},
                    {"value", "Hylla"},
                    {"is_display", true},
                },
                {
                    {"key", "FSN"},
                    {"value", "FSN123"},
                    {"is_display", true},
                },
            })},
        };
        window.handleUIMessage(cookie_message);
        QApplication::processEvents();

        require(info_label->text().contains(QStringLiteral("1082")),
                "dialog should refresh product id from node cookies");
        require(info_label->text().contains(QStringLiteral("Hylla")),
                "dialog should refresh device name from node cookies");
        require(info_label->text().contains(QStringLiteral("FSN123")),
                "dialog should refresh fsn from node cookies");

        auto* cookie_view = workspace->dataPage()->findChild<QPlainTextEdit*>(QStringLiteral("node_cookie_view"));
        require(cookie_view != nullptr, "node cookie view missing");
        require(cookie_view->toPlainText().contains(QStringLiteral("product_id: 1082")),
                "data page should mirror product id cookie");
        require(cookie_view->toPlainText().contains(QStringLiteral("name: Hylla")),
                "data page should mirror name cookie");
        require(cookie_view->toPlainText().contains(QStringLiteral("FSN: FSN123")),
                "data page should mirror fsn cookie");

        dialog->accept();
    });

    recordlab::host::HostMessage dialog_message;
    dialog_message.source = recordlab::host::msg::SCRIPTS_ACTUATOR;
    dialog_message.target = recordlab::host::msg::UI;
    dialog_message.type = recordlab::host::msg::UI_DIALOG_REQUEST;
    dialog_message.payload = {
        {"dialog_id", "dlg_live_cookie"},
        {"kind", "multi_field_input"},
        {"title", "参数填写"},
        {"message", "请输入批量录制参数"},
        {"live_cookie_card", {
            {"agent_name", "glasses_nviz_node"},
            {"base_message", "请输入批量录制参数"},
            {"card_style", "background-color:#FFFDF2;border:1px solid #C8B36A;padding:8px;"},
        }},
        {"fields", nlohmann::json::array({
            {
                {"name", "traj_list"},
                {"label", "轨迹列表"},
                {"default", "10-1"},
            },
        })},
    };
    window.handleDialogRequest(dialog_message);

    return 0;
}
