#include "recordlab_host/ui/sensor_workspace_widget.h"

#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>

namespace recordlab::host::ui {

namespace {

QString listStyle(const QString& background, const QString& border) {
    return QStringLiteral(
               "QListWidget { background-color: %1; border: 1px solid %2; padding: 4px; }"
               "QListWidget::item:selected { background-color: #d7e7ff; }")
        .arg(background, border);
}

}  // namespace

SensorWorkspaceWidget::SensorWorkspaceWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sensor_workspace_widget"));

    auto* root_layout = new QHBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(8);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(buildCenterPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({360, 980});
    root_layout->addWidget(splitter);
}

QListWidget* SensorWorkspaceWidget::dataSelectionList() const {
    return data_selection_list_;
}

QListWidget* SensorWorkspaceWidget::customDataList() const {
    return custom_data_list_;
}

QPlainTextEdit* SensorWorkspaceWidget::realtimeValueView() const {
    return realtime_value_view_;
}

QLabel* SensorWorkspaceWidget::motionStatusLabel() const {
    return motion_status_label_;
}

void SensorWorkspaceWidget::handleRealtimeData(const QString& data_name, const nlohmann::json& value, double frequency) {
    QString line;
    if (data_name == QStringLiteral("imu_data") && value.is_object()) {
        line = QStringLiteral("imu_data [%1Hz]\ntype:%2\n")
                   .arg(frequency, 0, 'f', 1)
                   .arg(value.value("type", 0));
        if (value.contains("data") && value["data"].is_array() && value["data"].size() >= 6) {
            line += QStringLiteral("gyro x:%1 y:%2 z:%3\nacc  x:%4 y:%5 z:%6")
                        .arg(value["data"][0].get<double>(), 0, 'f', 3)
                        .arg(value["data"][1].get<double>(), 0, 'f', 3)
                        .arg(value["data"][2].get<double>(), 0, 'f', 3)
                        .arg(value["data"][3].get<double>(), 0, 'f', 3)
                        .arg(value["data"][4].get<double>(), 0, 'f', 3)
                        .arg(value["data"][5].get<double>(), 0, 'f', 3);
        }
        realtime_value_view_->setPlainText(line);
        if (selected_data_label_) {
            selected_data_label_->setText(QStringLiteral("当前选择: imu_data"));
        }
        return;
    }
    if (data_name == QStringLiteral("motion_status")) {
        QString status = QStringLiteral("数据不足");
        if (value.is_object()) {
            status = QString::fromStdString(value.value("status", value.value("value", std::string("数据不足"))));
        } else if (value.is_string()) {
            status = QString::fromStdString(value.get<std::string>());
        }
        motion_status_label_->setText(QStringLiteral("运动状态: %1").arg(status));
        const bool moving = status.contains(QStringLiteral("moving"), Qt::CaseInsensitive)
            || status.contains(QStringLiteral("运动"));
        motion_status_label_->setStyleSheet(moving
            ? QStringLiteral("QLabel { background-color: #e8f5e9; color: #1b5e20; border: 1px solid #4caf50; padding: 6px; font-weight: 600; }")
            : QStringLiteral("QLabel { background-color: #eceff1; color: #37474f; border: 1px solid #78909c; padding: 6px; font-weight: 600; }"));
        return;
    }
    if (data_name == QStringLiteral("record_timer")) {
        return;
    }
    if (data_name == QStringLiteral("time_delay")) {
        return;
    }
}

QWidget* SensorWorkspaceWidget::buildLeftPanel() {
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("sensor_left_panel"));
    panel->setMinimumWidth(300);
    panel->setMaximumWidth(420);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* data_group = new QGroupBox(QStringLiteral("数据选择"), panel);
    data_group->setObjectName(QStringLiteral("data_selection_group"));
    auto* data_layout = new QVBoxLayout(data_group);
    data_selection_list_ = new QListWidget(data_group);
    data_selection_list_->setObjectName(QStringLiteral("data_selection_list"));
    data_selection_list_->setStyleSheet(listStyle(QStringLiteral("#fff7d8"), QStringLiteral("#a59470")));
    data_selection_list_->addItems({
        QStringLiteral("IMU0-gyro [x 0.0Hz]"),
        QStringLiteral("IMU0-acc [x 0.0Hz]"),
        QStringLiteral("IMU0-mag [x 0.0Hz]"),
        QStringLiteral("IMU0-temperature [x 0.0Hz]"),
        QStringLiteral("IMU1-gyro [x 0.0Hz]"),
        QStringLiteral("IMU1-acc [x 0.0Hz]"),
        QStringLiteral("Android-gyro [x 0.0Hz]"),
        QStringLiteral("Android-acc [x 0.0Hz]"),
    });
    data_layout->addWidget(data_selection_list_);
    layout->addWidget(data_group, 3);

    auto* custom_group = new QGroupBox(QStringLiteral("自定义数据"), panel);
    custom_group->setObjectName(QStringLiteral("custom_data_group"));
    auto* custom_layout = new QVBoxLayout(custom_group);
    custom_data_list_ = new QListWidget(custom_group);
    custom_data_list_->setObjectName(QStringLiteral("custom_data_list"));
    custom_data_list_->setStyleSheet(listStyle(QStringLiteral("#eef9ea"), QStringLiteral("#90a78c")));
    custom_data_list_->addItems({
        QStringLiteral("motion_status [x 0.0Hz]"),
        QStringLiteral("camera_data [x 0.0Hz]"),
    });
    custom_layout->addWidget(custom_data_list_);
    layout->addWidget(custom_group, 2);

    auto* realtime_group = new QGroupBox(QStringLiteral("实时值"), panel);
    realtime_group->setObjectName(QStringLiteral("realtime_values_group"));
    auto* realtime_layout = new QVBoxLayout(realtime_group);
    realtime_value_view_ = new QPlainTextEdit(realtime_group);
    realtime_value_view_->setObjectName(QStringLiteral("realtime_values_view"));
    realtime_value_view_->setReadOnly(true);
    realtime_value_view_->setPlainText(QStringLiteral(
        "IMU0-gyro\nx:-- y:-- z:--\n"
        "IMU0-acc\nx:-- y:-- z:--"));
    realtime_value_view_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: #f5f5f5; border: 1px solid #888; padding: 8px; font-family: monospace; }"));
    realtime_layout->addWidget(realtime_value_view_);
    layout->addWidget(realtime_group, 4);

    motion_status_label_ = new QLabel(QStringLiteral("运动状态: 数据不足"), panel);
    motion_status_label_->setObjectName(QStringLiteral("motion_status_label"));
    motion_status_label_->setAlignment(Qt::AlignCenter);
    motion_status_label_->setMinimumHeight(36);
    motion_status_label_->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #eceff1; color: #37474f; border: 1px solid #78909c; padding: 6px; font-weight: 600; }"));
    layout->addWidget(motion_status_label_, 0);

    return panel;
}

QWidget* SensorWorkspaceWidget::buildCenterPanel() {
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("sensor_center_panel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* camera_group = new QGroupBox(QStringLiteral("图像预览"), panel);
    camera_group->setObjectName(QStringLiteral("camera_preview_group"));
    auto* camera_layout = new QHBoxLayout(camera_group);
    camera_layout->setContentsMargins(8, 8, 8, 8);
    camera_layout->setSpacing(8);
    auto* video_1 = buildVideoPlaceholder(QStringLiteral("图像 1"));
    video_1->setObjectName(QStringLiteral("video_panel_1"));
    auto* video_2 = buildVideoPlaceholder(QStringLiteral("图像 2"));
    video_2->setObjectName(QStringLiteral("video_panel_2"));
    camera_layout->addWidget(video_1, 1);
    camera_layout->addWidget(video_2, 1);
    layout->addWidget(camera_group, 3);

    selected_data_label_ = new QLabel(QStringLiteral("当前选择: 未选择数据"), panel);
    selected_data_label_->setObjectName(QStringLiteral("selected_data_label"));
    selected_data_label_->setAlignment(Qt::AlignCenter);
    selected_data_label_->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #f0f0f0; border: 1px solid #888; padding: 5px; font-weight: 600; }"));
    layout->addWidget(selected_data_label_, 0);

    auto* curve_group = new QGroupBox(QStringLiteral("传感器数据曲线"), panel);
    curve_group->setObjectName(QStringLiteral("curve_preview_group"));
    auto* curve_layout = new QHBoxLayout(curve_group);
    curve_layout->setContentsMargins(8, 8, 8, 8);
    curve_layout->setSpacing(8);
    auto* curve_1 = buildCurvePlaceholder(QStringLiteral("曲线 1"));
    curve_1->setObjectName(QStringLiteral("curve_panel_1"));
    auto* curve_2 = buildCurvePlaceholder(QStringLiteral("曲线 2"));
    curve_2->setObjectName(QStringLiteral("curve_panel_2"));
    auto* curve_3 = buildCurvePlaceholder(QStringLiteral("曲线 3"));
    curve_3->setObjectName(QStringLiteral("curve_panel_3"));
    curve_layout->addWidget(curve_1, 1);
    curve_layout->addWidget(curve_2, 1);
    curve_layout->addWidget(curve_3, 1);
    layout->addWidget(curve_group, 2);

    return panel;
}

QWidget* SensorWorkspaceWidget::buildVideoPlaceholder(const QString& title) {
    auto* frame = new QFrame();
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral(R"(
        QFrame { background-color: #20242a; border: 1px solid #6d747d; }
        QLabel { color: #e6ecf2; }
    )"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 8, 8, 8);
    auto* label = new QLabel(title, frame);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumSize(220, 120);
    label->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600;"));
    layout->addWidget(label, 1);
    auto* status = new QLabel(QStringLiteral("等待图像流"), frame);
    status->setAlignment(Qt::AlignCenter);
    layout->addWidget(status);
    return frame;
}

QWidget* SensorWorkspaceWidget::buildCurvePlaceholder(const QString& title) {
    auto* frame = new QFrame();
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral("QFrame { background-color: #fffdf2; border: 1px solid #9a8b62; }"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 8, 8, 8);
    auto* label = new QLabel(title, frame);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(label);

    auto* canvas = new QLabel(frame);
    canvas->setMinimumSize(180, 110);
    canvas->setAlignment(Qt::AlignCenter);
    canvas->setText(QStringLiteral("等待实时数据进入当前窗口"));
    canvas->setStyleSheet(QStringLiteral("QLabel { background-color: #ffffe0; border: 1px solid #b6a86b; color: #5b5537; }"));
    layout->addWidget(canvas, 1);
    return frame;
}

}  // namespace recordlab::host::ui
