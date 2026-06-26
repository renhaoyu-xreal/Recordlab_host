#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QListWidget;
class QSpinBox;

namespace recordlab::host::ui {

class VirtualNodesPage : public QWidget {
    Q_OBJECT

public:
    explicit VirtualNodesPage(QWidget* parent = nullptr);

    bool isVirtualUrEnabled() const;
    int trajectoryDurationSeconds() const;
    int trajectoryFileSizeMiB() const;
    int trajectoryReturnRateMiBPerS() const;

    void setVirtualUrEnabled(bool enabled);

signals:
    void virtualUrToggleRequested(bool enabled);
    void virtualUrSettingsChanged(int trajectory_duration_s,
                                  int trajectory_file_size_mib,
                                  int trajectory_return_rate_mib_per_s);

private:
    void loadSettings();
    void saveDurationSetting(int value);
    void saveFileSizeSetting(int value);
    void saveReturnRateSetting(int value);
    void updateStatusText();

    QListWidget* node_list_ = nullptr;
    QLabel* ur_status_label_ = nullptr;
    QCheckBox* ur_toggle_ = nullptr;
    QSpinBox* trajectory_duration_spinbox_ = nullptr;
    QSpinBox* trajectory_file_size_spinbox_ = nullptr;
    QSpinBox* trajectory_return_rate_spinbox_ = nullptr;
};

}  // namespace recordlab::host::ui
