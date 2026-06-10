#pragma once

#include <QWidget>
#include <QStringList>

class QHBoxLayout;
class QLabel;
class QListWidget;
class QTabWidget;

namespace recordlab::host::ui {

class LogTextEdit;
class SensorWorkspaceWidget;

class ScriptPage : public QWidget {
    Q_OBJECT

public:
    explicit ScriptPage(QWidget* parent = nullptr);

    SensorWorkspaceWidget* sensorWorkspace() const;
    QListWidget* scriptList() const;
    LogTextEdit* logView() const;
    void setScripts(const QStringList& scripts);
    void setDataRoot(const QString& data_root);
    QStringList selectedScripts() const;
    void clearWorkflow();
    void showWorkflowTab();
    void updateWorkflow(const QString& title, const QString& message,
                        const QString& steps_json, bool finished, bool success);

signals:
    void runScriptRequested(const QString& script_path);
    void stopScriptRequested();

private:
    QWidget* buildWorkflowPanel();
    SensorWorkspaceWidget* sensor_workspace_ = nullptr;
    QListWidget* script_list_ = nullptr;
    LogTextEdit* log_view_ = nullptr;
    QTabWidget* output_tabs_ = nullptr;
    QWidget* workflow_panel_ = nullptr;
    QLabel* workflow_title_label_ = nullptr;
    QLabel* workflow_message_label_ = nullptr;
    QHBoxLayout* workflow_steps_layout_ = nullptr;
};

}  // namespace recordlab::host::ui
