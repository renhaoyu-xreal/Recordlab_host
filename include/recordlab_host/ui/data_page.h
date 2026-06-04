#pragma once

#include <QString>
#include <QWidget>

class QComboBox;
class QPlainTextEdit;

namespace recordlab::host::ui {

class SensorWorkspaceWidget;

class DataPage : public QWidget {
    Q_OBJECT

public:
    explicit DataPage(QWidget* parent = nullptr);

    SensorWorkspaceWidget* sensorWorkspace() const;
    QComboBox* agentSelector() const;
    QComboBox* commandComboBox() const;
    QPlainTextEdit* commandParamsEdit() const;
    QPlainTextEdit* logView() const;
    void setDataRoot(const QString& data_root);

signals:
    void commandRequested(const QString& cmd, const QString& params_json);
    void stopAllRequested();

private:
    SensorWorkspaceWidget* sensor_workspace_ = nullptr;
    QComboBox* agent_selector_ = nullptr;
    QComboBox* command_combo_box_ = nullptr;
    QPlainTextEdit* command_params_edit_ = nullptr;
    QPlainTextEdit* log_view_ = nullptr;
};

}  // namespace recordlab::host::ui
