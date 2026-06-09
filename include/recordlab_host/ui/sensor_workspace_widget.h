#pragma once

#include "recordlab_host/data/camera_shared_memory.h"

#include <nlohmann/json.hpp>
#include <QString>
#include <QWidget>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>

class QLabel;
class QGroupBox;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QTimer;

namespace recordlab::host::ui {

class SensorCurveWidget;

class SensorWorkspaceWidget : public QWidget {
    Q_OBJECT

public:
    explicit SensorWorkspaceWidget(QWidget* parent = nullptr);

    QListWidget* dataSelectionList() const;
    QListWidget* customDataList() const;
    QPlainTextEdit* realtimeValueView() const;
    QLabel* motionStatusLabel() const;
    void configureLayout(const nlohmann::json& sensor_layout);
    void handleRealtimeData(const QString& data_name, const nlohmann::json& value, double frequency);
    void handleSummaryData(const QString& data_name, const nlohmann::json& value);

private:
    QWidget* buildLeftPanel();
    QWidget* buildCenterPanel();
    QWidget* buildVideoPlaceholder(const QString& title, QLabel*& image_label, QLabel*& status_label);
    SensorCurveWidget* buildCurveWidget();
    void updateFrequencyLabels(const QString& data_name, const nlohmann::json& value, double frequency);
    void updateSelectedDataFromItem(QListWidgetItem* item);
    std::optional<std::array<double, 3>> appendCurveSample(const QString& key, const nlohmann::json& value);
    void updateRealtimeValueLine(const QString& label, const QString& value_text);
    void updateSummaryValueBlock(const QString& label, const QString& value_text);
    void renderRealtimeValues();
    void requestCurveRefresh();
    void refreshSelectedCurves();
    void handleCameraData(const nlohmann::json& value);
    void updateVideoFrame(int camera_index, const nlohmann::json& image_payload);
    void updateVideoFrameFromSharedMemory(int camera_index, const nlohmann::json& image_payload);
    void setVideoStatus(int camera_index, const QString& text, bool force = false);

    QListWidget* data_selection_list_ = nullptr;
    QListWidget* custom_data_list_ = nullptr;
    QPlainTextEdit* realtime_value_view_ = nullptr;
    QLabel* motion_status_label_ = nullptr;
    QGroupBox* curve_group_ = nullptr;
    QString selected_data_name_;
    std::unique_ptr<recordlab::host::CameraSharedMemoryReader> camera_shm_reader_;
    std::array<std::uint64_t, 2> last_camera_shm_seq_{};
    std::array<std::chrono::steady_clock::time_point, 2> last_video_status_update_{};
    std::array<QString, 2> last_video_status_text_{};
    std::array<QLabel*, 2> video_image_labels_{};
    std::array<QLabel*, 2> video_status_labels_{};
    SensorCurveWidget* curve_widget_ = nullptr;
    std::map<QString, std::deque<std::array<double, 4>>> curve_history_;
    std::map<QString, QString> realtime_value_lines_;
    std::map<QString, QString> summary_value_blocks_;
    std::map<QString, std::array<double, 3>> smoothed_value_by_label_;
    QTimer* curve_refresh_timer_ = nullptr;
    bool curve_dirty_ = false;
    nlohmann::json sensor_layout_ = nlohmann::json::object();
    std::map<QString, QString> stream_label_by_key_;
    std::map<QString, QString> label_key_by_label_;
    std::map<QString, int> list_row_by_label_;
    std::set<QString> channel_stream_keys_;
    std::set<QString> summary_only_labels_;
    std::set<QString> labels_with_data_;
};

}  // namespace recordlab::host::ui
