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
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <exception>
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

bool jsonNumberAt(const nlohmann::json& value, std::size_t index, double& out) {
    if (!value.is_object() || !value.contains("data") || !value["data"].is_array() || value["data"].size() <= index) {
        return false;
    }
    const auto& item = value["data"][index];
    if (!item.is_number()) {
        return false;
    }
    out = item.get<double>();
    return std::isfinite(out);
}

}  // namespace

class SensorCurveWidget : public QWidget {
public:
    explicit SensorCurveWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setProperty("curve_sample_count", 0);
    }

    void setSeries(const QString& data_name, const std::deque<std::array<double, 4>>& samples, bool scalar_mode) {
        selected_data_name_ = data_name;
        scalar_mode_ = scalar_mode;
        samples_ = {};
        samples_.reserve(static_cast<int>(samples.size()));
        for (const auto& sample : samples) {
            if (std::isfinite(sample[0]) && std::isfinite(sample[1])
                && std::isfinite(sample[2]) && std::isfinite(sample[3])) {
                samples_.push_back(sample);
            }
        }
        setProperty("curve_sample_count", samples_.size());
        update();
    }

    void clearSeries(const QString& data_name = {}) {
        selected_data_name_ = data_name;
        samples_.clear();
        setProperty("curve_sample_count", 0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.fillRect(rect(), QColor(QStringLiteral("#fafafa")));

        const QRect frame = rect().adjusted(8, 8, -8, -8);
        painter.setPen(QPen(QColor(QStringLiteral("#c9c9c9"))));
        painter.drawRect(frame);

        if (samples_.isEmpty()) {
            painter.setPen(QColor(QStringLiteral("#666666")));
            const QString placeholder = selected_data_name_.isEmpty()
                ? QStringLiteral("选择 IMU / time_delay / record_timer 后显示实时曲线。")
                : QStringLiteral("%1\n等待数据...").arg(selected_data_name_);
            painter.drawText(frame.adjusted(12, 12, -12, -12),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             placeholder);
            return;
        }

        const std::array<QString, 3> titles = {
            scalar_mode_ ? QStringLiteral("Value") : QStringLiteral("X / Value"),
            QStringLiteral("Y"),
            QStringLiteral("Z"),
        };
        const std::array<QColor, 3> colors = {
            QColor(QStringLiteral("#cc453a")),
            QColor(QStringLiteral("#2b8a57")),
            QColor(QStringLiteral("#2d69c7")),
        };
        const std::array<QColor, 3> backgrounds = {
            QColor(QStringLiteral("#fff1ef")),
            QColor(QStringLiteral("#effbf4")),
            QColor(QStringLiteral("#eef4ff")),
        };

        const int spacing = 10;
        const int panel_width = std::max(80, (frame.width() - spacing * 2) / 3);
        const int title_height = 24;
        const int axis_footer_height = 26;
        const int stats_height = 42;
        const int chart_top = frame.top() + title_height + 6;
        const int chart_bottom = frame.bottom() - axis_footer_height - stats_height - 8;
        const int chart_height = std::max(34, chart_bottom - chart_top);
        const double visible_start = samples_.front()[0];
        const double visible_end = std::max(visible_start, samples_.back()[0]);
        const double visible_span = std::max(0.001, visible_end - visible_start);

        for (int axis = 0; axis < 3; ++axis) {
            const QRect panel_rect(frame.left() + axis * (panel_width + spacing),
                                   frame.top(), panel_width, frame.height());
            const QRect chart_rect(panel_rect.left() + 44, chart_top,
                                   panel_rect.width() - 56, chart_height);
            const QRect stats_rect(panel_rect.left() + 8, panel_rect.bottom() - stats_height,
                                   panel_rect.width() - 16, stats_height - 4);

            painter.fillRect(panel_rect, backgrounds[axis]);
            painter.setPen(QPen(QColor(QStringLiteral("#d6d6d6"))));
            painter.drawRect(panel_rect);
            painter.setPen(QColor(QStringLiteral("#4f4f4f")));
            painter.drawText(panel_rect.adjusted(8, 4, -8, -4),
                             Qt::AlignTop | Qt::AlignHCenter, titles[axis]);

            if (scalar_mode_ && axis > 0) {
                painter.setPen(QColor(QStringLiteral("#999999")));
                painter.drawText(chart_rect, Qt::AlignCenter, QStringLiteral("标量数据"));
                painter.drawText(stats_rect, Qt::AlignCenter, QStringLiteral("--"));
                continue;
            }

            double min_value = sampleValue(samples_.front(), axis);
            double max_value = min_value;
            double sum = 0.0;
            double sum_squares = 0.0;
            for (const auto& sample : samples_) {
                const double value = sampleValue(sample, axis);
                min_value = std::min(min_value, value);
                max_value = std::max(max_value, value);
                sum += value;
                sum_squares += value * value;
            }
            const double count = static_cast<double>(samples_.size());
            const double mean = sum / count;
            const double variance = std::max(0.0, sum_squares / count - mean * mean);
            const double std_value = std::sqrt(variance);
            const double pkpk = max_value - min_value;
            double margin = (max_value - min_value) * 0.15;
            if (margin < 1e-6) margin = 1.0;
            min_value -= margin;
            max_value += margin;

            painter.setPen(QPen(QColor(QStringLiteral("#8b8b8b")), 1));
            painter.drawLine(chart_rect.bottomLeft(), chart_rect.bottomRight());
            painter.drawLine(chart_rect.bottomLeft(), chart_rect.topLeft());

            painter.setPen(QPen(QColor(QStringLiteral("#e1e1e1")), 1, Qt::DashLine));
            for (int tick = 0; tick <= 2; ++tick) {
                const qreal ratio = tick / 2.0;
                const int y = chart_rect.bottom() - static_cast<int>(ratio * chart_rect.height());
                painter.drawLine(chart_rect.left(), y, chart_rect.right(), y);
                const double tick_value = min_value + ratio * (max_value - min_value);
                painter.setPen(QColor(QStringLiteral("#6b6b6b")));
                painter.drawText(QRect(panel_rect.left() + 2, y - 10, 38, 20),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QStringLiteral("%1").arg(tick_value, 0, 'f', 2));
                painter.setPen(QPen(QColor(QStringLiteral("#e1e1e1")), 1, Qt::DashLine));
            }
            for (int tick = 0; tick <= 2; ++tick) {
                const qreal ratio = tick / 2.0;
                const int x = chart_rect.left() + static_cast<int>(ratio * chart_rect.width());
                painter.drawLine(x, chart_rect.top(), x, chart_rect.bottom());
            }

            QPainterPath path;
            for (int i = 0; i < samples_.size(); ++i) {
                const auto& sample = samples_[i];
                const double relative_timestamp = std::max(0.0, sample[0] - visible_start);
                const double time_ratio = std::clamp(relative_timestamp / visible_span, 0.0, 1.0);
                const double value = sampleValue(sample, axis);
                const double denominator = max_value - min_value;
                const double value_ratio = denominator <= 1e-9 ? 0.5 : (value - min_value) / denominator;
                const qreal x = chart_rect.left() + time_ratio * chart_rect.width();
                const qreal y = chart_rect.bottom() - value_ratio * chart_rect.height();
                if (i == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }

            painter.setPen(QPen(colors[axis], 2));
            painter.drawPath(path);
            painter.setPen(QColor(QStringLiteral("#6b6b6b")));
            painter.drawText(QRect(chart_rect.left(), chart_rect.bottom() + 4, 72, 18),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("%1").arg(visible_start, 0, 'f', 1));
            painter.drawText(QRect(chart_rect.center().x() - 44, chart_rect.bottom() + 4, 88, 18),
                             Qt::AlignHCenter | Qt::AlignVCenter,
                             QStringLiteral("%1").arg(visible_start + visible_span / 2.0, 0, 'f', 1));
            painter.drawText(QRect(chart_rect.right() - 72, chart_rect.bottom() + 4, 72, 18),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QStringLiteral("%1").arg(visible_end, 0, 'f', 1));
            painter.setPen(colors[axis].darker(105));
            painter.drawText(stats_rect, Qt::AlignCenter | Qt::TextWordWrap,
                             QStringLiteral("mean:%1\nstd:%2  pkpk:%3")
                                .arg(mean, 0, 'f', 6)
                                .arg(std_value, 0, 'f', 6)
                                .arg(pkpk, 0, 'f', 6));
        }
    }

private:
    double sampleValue(const std::array<double, 4>& sample, int axis) const {
        return sample[static_cast<std::size_t>(axis + 1)];
    }

    QString selected_data_name_;
    bool scalar_mode_ = false;
    QVector<std::array<double, 4>> samples_;
};

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
    splitter->setSizes({320, 1040});
    root_layout->addWidget(splitter);

    curve_refresh_timer_ = new QTimer(this);
    curve_refresh_timer_->setInterval(16);
    connect(curve_refresh_timer_, &QTimer::timeout, this, [this]() {
        try {
            if (!curve_dirty_) {
                return;
            }
            curve_dirty_ = false;
            refreshSelectedCurves();
        } catch (const std::exception& e) {
            common::Logger::instance().log(common::LogLevel::Error, "SensorWorkspaceWidget",
                                           std::string("curve refresh failed: ") + e.what());
        } catch (...) {
            common::Logger::instance().log(common::LogLevel::Error, "SensorWorkspaceWidget",
                                           "curve refresh failed: unknown exception");
        }
    });
    curve_refresh_timer_->start();
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

void SensorWorkspaceWidget::configureLayout(const nlohmann::json& sensor_layout) {
    sensor_layout_ = sensor_layout.is_object() ? sensor_layout : nlohmann::json::object();
    stream_label_by_key_.clear();
    label_key_by_label_.clear();
    list_row_by_label_.clear();
    curve_history_.clear();
    realtime_value_lines_.clear();
    selected_data_name_.clear();
    if (curve_widget_) curve_widget_->clearSeries();
    if (!data_selection_list_ || !custom_data_list_ || sensor_layout_.empty()) {
        return;
    }
    data_selection_list_->clear();
    custom_data_list_->clear();
    const auto addItem = [&](QListWidget* list, const QString& label, const QString& stream_key) {
        const int row = list->count();
        list->addItem(QStringLiteral("%1 [--Hz]").arg(label));
        list_row_by_label_[label] = row;
        label_key_by_label_[label] = stream_key;
        stream_label_by_key_[stream_key] = label;
    };
    const auto imu = sensor_layout_.find("imu_data");
    if (imu != sensor_layout_.end() && imu->is_object()) {
        for (const auto& channel : imu->value("channels", nlohmann::json::array())) {
            if (!channel.is_object()) continue;
            const int type = channel.value("type", 0);
            const QString label = QString::fromStdString(channel.value("label", std::string("imu_data")));
            addItem(data_selection_list_, label, QStringLiteral("imu_data:%1").arg(type));
        }
    }
    for (const auto& [topic, layout] : sensor_layout_.items()) {
        if (topic == "imu_data" || !layout.is_object()) continue;
        const QString label = QString::fromStdString(layout.value("display_name", topic));
        addItem(custom_data_list_, label, QString::fromStdString(topic));
    }
    renderRealtimeValues();
}

void SensorWorkspaceWidget::resetTopicData(const QString& data_name) {
    if (data_name.isEmpty()) {
        return;
    }
    QStringList labels_to_clear;
    if (data_name == QStringLiteral("imu_data")) {
        for (const auto& [label, stream_key] : label_key_by_label_) {
            if (stream_key.startsWith(QStringLiteral("imu_data:"))) {
                labels_to_clear << label;
            }
        }
    } else {
        const auto label_it = stream_label_by_key_.find(data_name);
        if (label_it != stream_label_by_key_.end()) {
            labels_to_clear << label_it->second;
        }
    }
    for (const auto& label : labels_to_clear) {
        curve_history_.erase(label);
        realtime_value_lines_.erase(label);
        if (selected_data_name_ == label && curve_widget_) {
            curve_widget_->clearSeries(label);
        }
    }
    if (data_name == QStringLiteral("camera_data")) {
        last_camera_shm_seq_ = {};
        for (int i = 0; i < static_cast<int>(video_status_labels_.size()); ++i) {
            if (video_status_labels_[static_cast<std::size_t>(i)]) {
                setVideoStatus(i, QStringLiteral("图像 %1 | 等待图像流 | -- x --").arg(i + 1), true);
            }
            if (video_image_labels_[static_cast<std::size_t>(i)]) {
                video_image_labels_[static_cast<std::size_t>(i)]->clear();
                video_image_labels_[static_cast<std::size_t>(i)]->setText(QStringLiteral("图像 %1").arg(i + 1));
            }
        }
    }
    renderRealtimeValues();
}

void SensorWorkspaceWidget::handleRealtimeData(const QString& data_name, const nlohmann::json& value, double frequency) {
    updateFrequencyLabels(data_name, value, frequency);
    if (data_name == QStringLiteral("imu_data") && value.is_object()) {
        const int type = value.value("type", 0);
        const QString stream_key = QStringLiteral("imu_data:%1").arg(type);
        const auto configured_label = stream_label_by_key_.find(stream_key);
        const QString label = configured_label == stream_label_by_key_.end()
            ? imuLabelForType(type)
            : configured_label->second;
        appendCurveSample(label, value);
        QString value_text = QStringLiteral("type:%1 ").arg(type);
        if (value.contains("data") && value["data"].is_array() && !value["data"].empty()) {
            if (isTemperatureType(type)) {
                value_text += QStringLiteral("temperature:%1")
                            .arg(value["data"][0].get<double>(), 0, 'f', 2);
            } else if (value["data"].size() >= 3) {
                value_text += QStringLiteral("x:%1 y:%2 z:%3")
                        .arg(value["data"][0].get<double>(), 0, 'f', 3)
                        .arg(value["data"][1].get<double>(), 0, 'f', 3)
                        .arg(value["data"][2].get<double>(), 0, 'f', 3);
            }
        }
        updateRealtimeValueLine(label, value_text.trimmed());
        if (selected_data_name_ == label) {
            if (isVisible()) {
                requestCurveRefresh();
            } else {
                refreshSelectedCurves();
            }
        }
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
    connect(data_selection_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                try {
                    updateSelectedDataFromItem(current);
                } catch (...) {
                }
            });
    data_layout->addWidget(data_selection_list_);
    layout->addWidget(data_group, 3);

    auto* custom_group = new QGroupBox(QStringLiteral("自定义数据"), panel);
    custom_group->setObjectName(QStringLiteral("custom_data_group"));
    auto* custom_layout = new QVBoxLayout(custom_group);
    custom_data_list_ = new QListWidget(custom_group);
    custom_data_list_->setObjectName(QStringLiteral("custom_data_list"));
    custom_data_list_->setStyleSheet(listStyle(QStringLiteral("#eef9ea"), QStringLiteral("#90a78c")));
    connect(custom_data_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                try {
                    updateSelectedDataFromItem(current);
                } catch (...) {
                }
            });
    custom_layout->addWidget(custom_data_list_);
    layout->addWidget(custom_group, 2);

    auto* realtime_group = new QGroupBox(QStringLiteral("实时值"), panel);
    realtime_group->setObjectName(QStringLiteral("realtime_values_group"));
    auto* realtime_layout = new QVBoxLayout(realtime_group);
    realtime_value_view_ = new QPlainTextEdit(realtime_group);
    realtime_value_view_->setObjectName(QStringLiteral("realtime_values_view"));
    realtime_value_view_->setReadOnly(true);
    realtime_value_view_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: #f5f5f5; border: 1px solid #888; padding: 8px; font-family: monospace; }"));
    renderRealtimeValues();
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

    curve_group_ = new QGroupBox(QStringLiteral("传感器数据曲线: 未选择数据"), panel);
    curve_group_->setObjectName(QStringLiteral("curve_preview_group"));
    auto* curve_layout = new QVBoxLayout(curve_group_);
    curve_layout->setContentsMargins(8, 8, 8, 8);
    curve_widget_ = buildCurveWidget();
    curve_widget_->setObjectName(QStringLiteral("curve_plot_widget"));
    curve_widget_->setProperty("curve_panel_count", 3);
    curve_layout->addWidget(curve_widget_, 1);
    layout->addWidget(curve_group_, 2);

    return panel;
}

QWidget* SensorWorkspaceWidget::buildVideoPlaceholder(const QString& title, QLabel*& image_label, QLabel*& status_label) {
    auto* frame = new QFrame();
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral(R"(
        QFrame { background-color: #f7f4ec; border: 1px solid #b6b0a4; }
        QLabel { color: #3b3832; }
    )"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);
    image_label = new QLabel(title, frame);
    image_label->setAlignment(Qt::AlignCenter);
    image_label->setMinimumSize(220, 120);
    image_label->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #101214; color: #e9edf0; border: 1px solid #2c3135; font-size: 16px; font-weight: 600; }"));
    layout->addWidget(image_label, 1);
    status_label = new QLabel(QStringLiteral("%1 | 等待图像流 | -- x --").arg(title), frame);
    status_label->setAlignment(Qt::AlignCenter);
    status_label->setMinimumHeight(24);
    status_label->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #fffdf2; color: #504a40; border: 1px solid #c9c0ae; padding: 3px 6px; font-family: monospace; }"));
    layout->addWidget(status_label);
    return frame;
}

void SensorWorkspaceWidget::updateFrequencyLabels(const QString& data_name, const nlohmann::json& value, double frequency) {
    if (value.is_object()) {
        const auto frequencies = value.find("_stream_frequencies_hz");
        if (frequencies != value.end() && frequencies->is_object()) {
            if (!stream_label_by_key_.empty()) {
                for (const auto& [key, frequency_value] : frequencies->items()) {
                    if (!frequency_value.is_number()) continue;
                    const QString stream_key = QString::fromStdString(key);
                    const auto label_it = stream_label_by_key_.find(stream_key);
                    if (label_it == stream_label_by_key_.end()) continue;
                    const auto row_it = list_row_by_label_.find(label_it->second);
                    if (row_it == list_row_by_label_.end()) continue;
                    QListWidget* list = stream_key.startsWith(QStringLiteral("imu_data:"))
                        ? data_selection_list_
                        : custom_data_list_;
                    setListItemText(list, row_it->second, label_it->second, frequency_value.get<double>());
                }
                if (data_name != QStringLiteral("imu_data")) {
                    return;
                }
            }
        }
    }

    QString stream_key = data_name;
    if (data_name == QStringLiteral("imu_data") && value.is_object()) {
        stream_key = QStringLiteral("imu_data:%1").arg(value.value("type", 0));
    }
    const auto label_it = stream_label_by_key_.find(stream_key);
    if (label_it == stream_label_by_key_.end()) {
        return;
    }
    const auto row_it = list_row_by_label_.find(label_it->second);
    if (row_it == list_row_by_label_.end()) {
        return;
    }
    QListWidget* list = stream_key.startsWith(QStringLiteral("imu_data:"))
        ? data_selection_list_
        : custom_data_list_;
    setListItemText(list, row_it->second, label_it->second, frequency);
}

void SensorWorkspaceWidget::updateSelectedDataFromItem(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    selected_data_name_ = dataNameFromListText(item->text());
    if (curve_group_) {
        curve_group_->setTitle(selected_data_name_.isEmpty()
            ? QStringLiteral("传感器数据曲线: 未选择数据")
            : QStringLiteral("传感器数据曲线: %1").arg(selected_data_name_));
    }
    refreshSelectedCurves();
}

void SensorWorkspaceWidget::appendCurveSample(const QString& key, const nlohmann::json& value) {
    if (key.isEmpty()) {
        return;
    }
    auto& history = curve_history_[key];
    constexpr std::size_t kMaxCurveSamples = 600;
    const int type = value.value("type", 0);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!jsonNumberAt(value, 0, x)) {
        return;
    }
    if (!isTemperatureType(type)) {
        if (!jsonNumberAt(value, 1, y) || !jsonNumberAt(value, 2, z)) {
            return;
        }
    }
    const double timestamp = value.value("timestamp", value.value("timestamp_ns", 0.0) / 1e9);
    const double display_timestamp = timestamp > 0.0
        ? timestamp
        : (history.empty() ? 0.0 : history.back()[0] + 0.01);
    if (!history.empty() &&
        (display_timestamp < history.back()[0] - 1.0 || display_timestamp > history.back()[0] + 10.0)) {
        history.clear();
        if (selected_data_name_ == key && curve_widget_) {
            curve_widget_->clearSeries(key);
        }
    }
    history.push_back({display_timestamp, x, y, z});
    while (history.size() > kMaxCurveSamples) {
        history.pop_front();
    }
}

void SensorWorkspaceWidget::updateRealtimeValueLine(const QString& label, const QString& value_text) {
    if (label.isEmpty() || label == QStringLiteral("imu_data")) {
        return;
    }
    realtime_value_lines_[label] = value_text.isEmpty() ? QStringLiteral("--") : value_text;
    renderRealtimeValues();
}

void SensorWorkspaceWidget::renderRealtimeValues() {
    if (!realtime_value_view_) {
        return;
    }
    QStringList lines;
    QStringList labels;
    if (data_selection_list_ && data_selection_list_->count() > 0) {
        for (int row = 0; row < data_selection_list_->count(); ++row) {
            labels << dataNameFromListText(data_selection_list_->item(row)->text());
        }
    }
    for (const auto& label : labels) {
        const auto it = realtime_value_lines_.find(label);
        QString value_text;
        if (it != realtime_value_lines_.end()) {
            value_text = it->second;
        } else if (label.contains(QStringLiteral("temperature"))) {
            value_text = QStringLiteral("temperature:--");
        } else {
            value_text = QStringLiteral("x:-- y:-- z:--");
        }
        lines << QStringLiteral("%1 %2").arg(label, value_text);
    }
    realtime_value_view_->setPlainText(lines.join(QStringLiteral("\n")));
}

void SensorWorkspaceWidget::requestCurveRefresh() {
    curve_dirty_ = true;
}

void SensorWorkspaceWidget::refreshSelectedCurves() {
    const auto history_it = curve_history_.find(selected_data_name_);
    if (!curve_widget_) {
        return;
    }
    if (history_it == curve_history_.end()) {
        curve_widget_->clearSeries(selected_data_name_);
        return;
    }
    curve_widget_->setSeries(selected_data_name_, history_it->second,
                             selected_data_name_.contains(QStringLiteral("temperature")));
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
        setVideoStatus(camera_index, QStringLiteral("图像数据无效"), true);
        return;
    }

    const QByteArray bytes = bytesFromJsonBinary(image_payload["data"]);
    const std::string encoding = image_payload.value("encoding", std::string{});
    QString format_text = QString::fromStdString(encoding.empty() ? std::string("raw") : encoding);
    QImage frame;
    if (!encoding.empty()) {
        if (!frame.loadFromData(bytes, encoding == "jpeg" ? "JPG" : encoding.c_str())) {
            setVideoStatus(camera_index, QStringLiteral("图像解码失败"), true);
            return;
        }
    } else {
        if (bytes_per_line <= 0 || bytes.size() < bytes_per_line * height) {
            setVideoStatus(camera_index, QStringLiteral("图像数据不完整"), true);
            return;
        }

        const int format_value = image_payload.value("format", -1);
        QImage::Format image_format = QImage::Format_Invalid;
        if (format_value == static_cast<int>(QImage::Format_Grayscale8) || bytes_per_line == width) {
            image_format = QImage::Format_Grayscale8;
            format_text = QStringLiteral("gray8");
        } else if (format_value == static_cast<int>(QImage::Format_RGB888) || bytes_per_line >= width * 3) {
            image_format = QImage::Format_RGB888;
            format_text = QStringLiteral("rgb888");
        }
        if (image_format == QImage::Format_Invalid) {
            setVideoStatus(camera_index, QStringLiteral("图像格式不支持"), true);
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
        setVideoStatus(camera_index, QStringLiteral("图像解码失败"), true);
        return;
    }

    image_label->setPixmap(QPixmap::fromImage(frame).scaled(
        image_label->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    setVideoStatus(camera_index, QStringLiteral("cam %1 | %2 x %3 | %4")
        .arg(camera_index)
        .arg(width)
        .arg(height)
        .arg(format_text));

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
        setVideoStatus(camera_index, QStringLiteral("共享内存帧不可用"), true);
        return;
    }

    QImage::Format image_format = QImage::Format_Invalid;
    if (frame.format == static_cast<int>(QImage::Format_Grayscale8) || frame.bytes_per_line == frame.width) {
        image_format = QImage::Format_Grayscale8;
    } else if (frame.format == static_cast<int>(QImage::Format_RGB888) || frame.bytes_per_line >= frame.width * 3) {
        image_format = QImage::Format_RGB888;
    }
    if (image_format == QImage::Format_Invalid) {
        setVideoStatus(camera_index, QStringLiteral("共享内存图像格式不支持"), true);
        return;
    }
    if (frame.width <= 0 || frame.height <= 0 || frame.bytes_per_line <= 0
        || frame.data.size() < frame.bytes_per_line * frame.height) {
        setVideoStatus(camera_index, QStringLiteral("共享内存图像数据不完整"), true);
        return;
    }

    const QImage image(
        reinterpret_cast<const uchar*>(frame.data.constData()),
        frame.width,
        frame.height,
        frame.bytes_per_line,
        image_format);
    if (image.isNull()) {
        setVideoStatus(camera_index, QStringLiteral("共享内存图像解码失败"), true);
        return;
    }

    image_label->setPixmap(QPixmap::fromImage(image).scaled(
        image_label->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    setVideoStatus(camera_index, QStringLiteral("cam %1 | %2 x %3 | shm seq %4")
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

void SensorWorkspaceWidget::setVideoStatus(int camera_index, const QString& text, bool force) {
    if (camera_index < 0 || camera_index >= static_cast<int>(video_status_labels_.size())) {
        return;
    }
    auto* status_label = video_status_labels_[static_cast<std::size_t>(camera_index)];
    if (!status_label) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    auto& last_update = last_video_status_update_[static_cast<std::size_t>(camera_index)];
    auto& last_text = last_video_status_text_[static_cast<std::size_t>(camera_index)];
    const double elapsed = std::chrono::duration<double>(now - last_update).count();
    if (!force && last_update.time_since_epoch().count() != 0 && elapsed < 1.0) {
        return;
    }
    if (!force && last_text == text) {
        return;
    }
    status_label->setText(text);
    last_text = text;
    last_update = now;
}

SensorCurveWidget* SensorWorkspaceWidget::buildCurveWidget() {
    return new SensorCurveWidget();
}

}  // namespace recordlab::host::ui
