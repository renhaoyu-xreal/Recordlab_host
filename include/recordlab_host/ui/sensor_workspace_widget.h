#pragma once

#include <nlohmann/json.hpp>
#include <QWidget>

class QLabel;
class QListWidget;
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
    QWidget* buildVideoPlaceholder(const QString& title);
    QWidget* buildCurvePlaceholder(const QString& title);

    QListWidget* data_selection_list_ = nullptr;
    QListWidget* custom_data_list_ = nullptr;
    QPlainTextEdit* realtime_value_view_ = nullptr;
    QLabel* motion_status_label_ = nullptr;
    QLabel* selected_data_label_ = nullptr;
};

}  // namespace recordlab::host::ui
