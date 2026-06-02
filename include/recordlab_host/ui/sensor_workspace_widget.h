#pragma once

#include "recordlab_host/data/camera_shared_memory.h"

#include <nlohmann/json.hpp>
#include <QString>
#include <QWidget>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;

namespace recordlab::host::ui {

class SensorWorkspaceWidget : public QWidget {
    Q_OBJECT

public:
    explicit SensorWorkspaceWidget(QWidget* parent = nullptr);

    QListWidget* dataSelectionList() const;
    QListWidget* customDataList() const;
    QPlainTextEdit* realtimeValueView() const;
    QLabel* motionStatusLabel() const;
    void handleRealtimeData(const QString& data_name, const nlohmann::json& value, double frequency);

private:
    QWidget* buildLeftPanel();
    QWidget* buildCenterPanel();
    QWidget* buildVideoPlaceholder(const QString& title, QLabel*& image_label, QLabel*& status_label);
    QWidget* buildCurvePlaceholder(const QString& title);
    void updateFrequencyLabels(const QString& data_name, const nlohmann::json& value, double frequency);
    void updateSelectedDataFromItem(QListWidgetItem* item);
    void handleCameraData(const nlohmann::json& value);
    void updateVideoFrame(int camera_index, const nlohmann::json& image_payload);
    void updateVideoFrameFromSharedMemory(int camera_index, const nlohmann::json& image_payload);
    void setVideoStatus(int camera_index, const QString& text, bool force = false);

    QListWidget* data_selection_list_ = nullptr;
    QListWidget* custom_data_list_ = nullptr;
    QPlainTextEdit* realtime_value_view_ = nullptr;
    QLabel* motion_status_label_ = nullptr;
    QLabel* selected_data_label_ = nullptr;
    QString selected_data_name_;
    std::unique_ptr<recordlab::host::CameraSharedMemoryReader> camera_shm_reader_;
    std::array<std::uint64_t, 2> last_camera_shm_seq_{};
    std::array<std::chrono::steady_clock::time_point, 2> last_video_status_update_{};
    std::array<QString, 2> last_video_status_text_{};
    std::array<QLabel*, 2> video_image_labels_{};
    std::array<QLabel*, 2> video_status_labels_{};
};

}  // namespace recordlab::host::ui
