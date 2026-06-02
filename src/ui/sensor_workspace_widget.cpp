#include "recordlab_host/ui/sensor_workspace_widget.h"
#include "recordlab_host/common/logger.h"

#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QByteArray>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace recordlab::host::ui {

namespace {

QString listStyle(const QString& background, const QString& border) {
    return QStringLiteral(
               "QListWidget { background-color: %1; border: 1px solid %2; padding: 4px; }"
               "QListWidget::item:selected { background-color: #d7e7ff; }")
        .arg(background, border);
}

QByteArray bytesFromJsonBinary(const nlohmann::json& value) {
    if (value.is_object()) {
        const auto marker = value.find("__echo_bytes_base64__");
        if (marker != value.end() && marker->is_string()) {
            return QByteArray::fromBase64(QByteArray::fromStdString(marker->get<std::string>()));
        }
    }
    if (value.is_string()) {
        return QByteArray::fromBase64(QByteArray::fromStdString(value.get<std::string>()));
    }
    if (value.is_array()) {
        QByteArray bytes;
        bytes.reserve(static_cast<int>(value.size()));
        for (const auto& item : value) {
            if (item.is_number_integer()) {
                bytes.append(static_cast<char>(item.get<int>() & 0xff));
            }
        }
        return bytes;
    }
    return {};
}

QString imuLabelForType(int type) {
    switch (type) {
    case 1: return QStringLiteral("IMU0-gyro");
    case 2: return QStringLiteral("IMU0-acc");
    case 3: return QStringLiteral("IMU0-mag");
    case 4: return QStringLiteral("IMU1-gyro");
    case 5: return QStringLiteral("IMU1-acc");
    case 12: return QStringLiteral("IMU0-temperature");
    case 13: return QStringLiteral("IMU1-temperature");
    default: return QStringLiteral("imu_data");
    }
}

bool isTemperatureType(int type) {
    return type == 12 || type == 13;
}

QString frequencyText(double frequency) {
    if (frequency <= 0.0) {
        return QStringLiteral("--Hz");
    }
    return QStringLiteral("%1Hz").arg(frequency, 0, 'f', frequency >= 100.0 ? 0 : 1);
}

void setListItemText(QListWidget* list, int row, const QString& name, double frequency) {
    if (!list || row < 0 || row >= list->count()) {
        return;
    }
    list->item(row)->setText(QStringLiteral("%1 [%2]").arg(name, frequencyText(frequency)));
}

QString dataNameFromListText(const QString& text) {
    const int frequency_pos = text.indexOf(QStringLiteral(" ["));
    return (frequency_pos >= 0 ? text.left(frequency_pos) : text).trimmed();
}

}  // namespace

SensorWorkspaceWidget::SensorWorkspaceWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sensor_workspace_widget"));
    camera_shm_reader_ = std::make_unique<recordlab::host::CameraSharedMemoryReader>();

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
    updateFrequencyLabels(data_name, value, frequency);
    QString line;
    if (data_name == QStringLiteral("imu_data") && value.is_object()) {
        const int type = value.value("type", 0);
        const QString label = imuLabelForType(type);
        line = QStringLiteral("%1\ntype:%2\n").arg(label).arg(type);
        if (value.contains("data") && value["data"].is_array() && !value["data"].empty()) {
            if (isTemperatureType(type)) {
                line += QStringLiteral("temperature:%1")
                            .arg(value["data"][0].get<double>(), 0, 'f', 2);
            } else if (value["data"].size() >= 3) {
                line += QStringLiteral("x:%1 y:%2 z:%3")
                        .arg(value["data"][0].get<double>(), 0, 'f', 3)
                        .arg(value["data"][1].get<double>(), 0, 'f', 3)
                        .arg(value["data"][2].get<double>(), 0, 'f', 3);
            }
        }
        realtime_value_view_->setPlainText(line);
        return;
    }
    if (data_name == QStringLiteral("camera_data") && value.is_object()) {
        handleCameraData(value);
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
        QStringLiteral("IMU0-gyro [--Hz]"),
        QStringLiteral("IMU0-acc [--Hz]"),
        QStringLiteral("IMU0-mag [--Hz]"),
        QStringLiteral("IMU0-temperature [--Hz]"),
        QStringLiteral("IMU1-gyro [--Hz]"),
        QStringLiteral("IMU1-acc [--Hz]"),
        QStringLiteral("IMU1-temperature [--Hz]"),
    });
    connect(data_selection_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                updateSelectedDataFromItem(current);
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
        QStringLiteral("motion_status [--Hz]"),
        QStringLiteral("camera_data [--Hz]"),
    });
    connect(custom_data_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                updateSelectedDataFromItem(current);
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
    auto* video_1 = buildVideoPlaceholder(QStringLiteral("图像 1"), video_image_labels_[0], video_status_labels_[0]);
    video_1->setObjectName(QStringLiteral("video_panel_1"));
    auto* video_2 = buildVideoPlaceholder(QStringLiteral("图像 2"), video_image_labels_[1], video_status_labels_[1]);
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

QWidget* SensorWorkspaceWidget::buildVideoPlaceholder(const QString& title, QLabel*& image_label, QLabel*& status_label) {
    auto* frame = new QFrame();
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral(R"(
        QFrame { background-color: #20242a; border: 1px solid #6d747d; }
        QLabel { color: #e6ecf2; }
    )"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 8, 8, 8);
    image_label = new QLabel(title, frame);
    image_label->setAlignment(Qt::AlignCenter);
    image_label->setMinimumSize(220, 120);
    image_label->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600;"));
    layout->addWidget(image_label, 1);
    status_label = new QLabel(QStringLiteral("等待图像流"), frame);
    status_label->setAlignment(Qt::AlignCenter);
    layout->addWidget(status_label);
    return frame;
}

void SensorWorkspaceWidget::updateFrequencyLabels(const QString& data_name, const nlohmann::json& value, double frequency) {
    if (value.is_object()) {
        const auto frequencies = value.find("_stream_frequencies_hz");
        if (frequencies != value.end() && frequencies->is_object()) {
            const auto updateFromMap = [frequencies](QListWidget* list, int row, const QString& label, const char* key) {
                const auto it = frequencies->find(key);
                if (it != frequencies->end() && it->is_number()) {
                    setListItemText(list, row, label, it->get<double>());
                }
            };
            updateFromMap(data_selection_list_, 0, QStringLiteral("IMU0-gyro"), "imu:1");
            updateFromMap(data_selection_list_, 1, QStringLiteral("IMU0-acc"), "imu:2");
            updateFromMap(data_selection_list_, 2, QStringLiteral("IMU0-mag"), "imu:3");
            updateFromMap(data_selection_list_, 3, QStringLiteral("IMU0-temperature"), "imu:12");
            updateFromMap(data_selection_list_, 4, QStringLiteral("IMU1-gyro"), "imu:4");
            updateFromMap(data_selection_list_, 5, QStringLiteral("IMU1-acc"), "imu:5");
            updateFromMap(data_selection_list_, 6, QStringLiteral("IMU1-temperature"), "imu:13");
            updateFromMap(custom_data_list_, 0, QStringLiteral("motion_status"), "motion_status");
            updateFromMap(custom_data_list_, 1, QStringLiteral("camera_data"), "camera_data");
            return;
        }
    }

    if (data_name == QStringLiteral("imu_data") && value.is_object()) {
        const int type = value.value("type", 0);
        switch (type) {
        case 1: setListItemText(data_selection_list_, 0, QStringLiteral("IMU0-gyro"), frequency); break;
        case 2: setListItemText(data_selection_list_, 1, QStringLiteral("IMU0-acc"), frequency); break;
        case 3: setListItemText(data_selection_list_, 2, QStringLiteral("IMU0-mag"), frequency); break;
        case 12: setListItemText(data_selection_list_, 3, QStringLiteral("IMU0-temperature"), frequency); break;
        case 4: setListItemText(data_selection_list_, 4, QStringLiteral("IMU1-gyro"), frequency); break;
        case 5: setListItemText(data_selection_list_, 5, QStringLiteral("IMU1-acc"), frequency); break;
        case 13: setListItemText(data_selection_list_, 6, QStringLiteral("IMU1-temperature"), frequency); break;
        default: break;
        }
        return;
    }
    if (data_name == QStringLiteral("motion_status")) {
        setListItemText(custom_data_list_, 0, QStringLiteral("motion_status"), frequency);
    } else if (data_name == QStringLiteral("camera_data")) {
        setListItemText(custom_data_list_, 1, QStringLiteral("camera_data"), frequency);
    }
}

void SensorWorkspaceWidget::updateSelectedDataFromItem(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    selected_data_name_ = dataNameFromListText(item->text());
    if (!selected_data_label_) {
        return;
    }
    selected_data_label_->setText(selected_data_name_.isEmpty()
        ? QStringLiteral("当前选择: 未选择数据")
        : QStringLiteral("当前选择: %1").arg(selected_data_name_));
}

void SensorWorkspaceWidget::handleCameraData(const nlohmann::json& value) {
    const auto cam_data = value.find("cam_data");
    if (cam_data == value.end() || !cam_data->is_object()) {
        return;
    }

    int fallback_index = 0;
    for (const auto& [key, cam_info] : cam_data->items()) {
        if (!cam_info.is_object()) {
            continue;
        }
        int camera_index = fallback_index;
        try {
            camera_index = std::stoi(key);
        } catch (...) {
            camera_index = fallback_index;
        }
        fallback_index = std::min(fallback_index + 1, 1);
        const auto image = cam_info.find("image");
        const auto image_raw = cam_info.find("image_raw");
        if (image != cam_info.end() && image->is_object()) {
            updateVideoFrame(camera_index, *image);
        } else if (image_raw != cam_info.end() && image_raw->is_object()) {
            updateVideoFrame(camera_index, *image_raw);
        }
    }

}

void SensorWorkspaceWidget::updateVideoFrame(int camera_index, const nlohmann::json& image_payload) {
    const auto render_start = std::chrono::steady_clock::now();
    if (camera_index < 0 || camera_index >= static_cast<int>(video_image_labels_.size())) {
        return;
    }

    auto* image_label = video_image_labels_[camera_index];
    auto* status_label = video_status_labels_[camera_index];
    if (!image_label || !status_label) {
        return;
    }

    if (image_payload.value("shm", false) || image_payload.contains("shm_seq")) {
        updateVideoFrameFromSharedMemory(camera_index, image_payload);
        return;
    }

    const int width = image_payload.value("width", 0);
    const int height = image_payload.value("height", 0);
    const int bytes_per_line = image_payload.value("bytes_per_line", 0);
    if (width <= 0 || height <= 0 || !image_payload.contains("data")) {
        status_label->setText(QStringLiteral("图像数据无效"));
        return;
    }

    const QByteArray bytes = bytesFromJsonBinary(image_payload["data"]);
    const std::string encoding = image_payload.value("encoding", std::string{});
    QImage frame;
    if (!encoding.empty()) {
        if (!frame.loadFromData(bytes, encoding == "jpeg" ? "JPG" : encoding.c_str())) {
            status_label->setText(QStringLiteral("图像解码失败"));
            return;
        }
    } else {
        if (bytes_per_line <= 0 || bytes.size() < bytes_per_line * height) {
            status_label->setText(QStringLiteral("图像数据不完整"));
            return;
        }

        const int format_value = image_payload.value("format", -1);
        QImage::Format image_format = QImage::Format_Invalid;
        if (format_value == static_cast<int>(QImage::Format_Grayscale8) || bytes_per_line == width) {
            image_format = QImage::Format_Grayscale8;
        } else if (format_value == static_cast<int>(QImage::Format_RGB888) || bytes_per_line >= width * 3) {
            image_format = QImage::Format_RGB888;
        }
        if (image_format == QImage::Format_Invalid) {
            status_label->setText(QStringLiteral("图像格式不支持"));
            return;
        }

        const QImage image(
            reinterpret_cast<const uchar*>(bytes.constData()),
            width,
            height,
            bytes_per_line,
            image_format);
        frame = image.copy();
    }
    if (frame.isNull()) {
        status_label->setText(QStringLiteral("图像解码失败"));
        return;
    }

    image_label->setPixmap(QPixmap::fromImage(frame).scaled(
        image_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    image_label->setStyleSheet(QStringLiteral("background-color: #111418;"));
    status_label->setText(QStringLiteral("cam %1 | %2 x %3").arg(camera_index).arg(width).arg(height));

    static std::array<std::chrono::steady_clock::time_point, 2> last_debug_log{};
    const auto now = std::chrono::steady_clock::now();
    const double since_debug = std::chrono::duration<double>(now - last_debug_log[static_cast<std::size_t>(camera_index)]).count();
    if (since_debug >= 1.0) {
        const double render_ms = std::chrono::duration<double, std::milli>(now - render_start).count();
        common::Logger::instance().log(
            common::LogLevel::Debug,
            "SensorWorkspaceWidget",
            "camera render cam=" + std::to_string(camera_index)
                + " size=" + std::to_string(width) + "x" + std::to_string(height)
                + " bytes=" + std::to_string(bytes.size())
                + " render_ms=" + std::to_string(render_ms));
        last_debug_log[static_cast<std::size_t>(camera_index)] = now;
    }
}

void SensorWorkspaceWidget::updateVideoFrameFromSharedMemory(int camera_index, const nlohmann::json& image_payload) {
    const auto render_start = std::chrono::steady_clock::now();
    if (camera_index < 0 || camera_index >= static_cast<int>(video_image_labels_.size())) {
        return;
    }

    auto* image_label = video_image_labels_[camera_index];
    auto* status_label = video_status_labels_[camera_index];
    if (!image_label || !status_label || !camera_shm_reader_) {
        return;
    }

    const std::string shm_name = image_payload.value("shm_name", std::string("recordlab_camera_shm_v1"));
    const auto requested_seq = image_payload.value("shm_seq", static_cast<std::uint64_t>(0));
    auto& last_seq = last_camera_shm_seq_[static_cast<std::size_t>(camera_index)];
    if (requested_seq > 0 && requested_seq > last_seq) {
        last_seq = requested_seq - 1;
    }

    recordlab::host::CameraSharedFrame frame;
    if (!camera_shm_reader_->readLatestFrame(shm_name, camera_index, last_seq, frame)) {
        status_label->setText(QStringLiteral("共享内存帧不可用"));
        return;
    }

    QImage::Format image_format = QImage::Format_Invalid;
    if (frame.format == static_cast<int>(QImage::Format_Grayscale8) || frame.bytes_per_line == frame.width) {
        image_format = QImage::Format_Grayscale8;
    } else if (frame.format == static_cast<int>(QImage::Format_RGB888) || frame.bytes_per_line >= frame.width * 3) {
        image_format = QImage::Format_RGB888;
    }
    if (image_format == QImage::Format_Invalid) {
        status_label->setText(QStringLiteral("共享内存图像格式不支持"));
        return;
    }
    if (frame.width <= 0 || frame.height <= 0 || frame.bytes_per_line <= 0
        || frame.data.size() < frame.bytes_per_line * frame.height) {
        status_label->setText(QStringLiteral("共享内存图像数据不完整"));
        return;
    }

    const QImage image(
        reinterpret_cast<const uchar*>(frame.data.constData()),
        frame.width,
        frame.height,
        frame.bytes_per_line,
        image_format);
    const QImage copied = image.copy();
    if (copied.isNull()) {
        status_label->setText(QStringLiteral("共享内存图像解码失败"));
        return;
    }

    image_label->setPixmap(QPixmap::fromImage(copied).scaled(
        image_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    image_label->setStyleSheet(QStringLiteral("background-color: #111418;"));
    status_label->setText(QStringLiteral("cam %1 | %2 x %3 | shm %4")
        .arg(camera_index)
        .arg(frame.width)
        .arg(frame.height)
        .arg(frame.seq));

    static std::array<std::chrono::steady_clock::time_point, 2> last_debug_log{};
    const auto now = std::chrono::steady_clock::now();
    const double since_debug = std::chrono::duration<double>(
        now - last_debug_log[static_cast<std::size_t>(camera_index)]).count();
    if (since_debug >= 1.0) {
        const double render_ms = std::chrono::duration<double, std::milli>(now - render_start).count();
        common::Logger::instance().log(
            common::LogLevel::Debug,
            "SensorWorkspaceWidget",
            "camera shm render cam=" + std::to_string(camera_index)
                + " size=" + std::to_string(frame.width) + "x" + std::to_string(frame.height)
                + " bytes=" + std::to_string(frame.data.size())
                + " seq=" + std::to_string(frame.seq)
                + " render_ms=" + std::to_string(render_ms));
        last_debug_log[static_cast<std::size_t>(camera_index)] = now;
    }
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
