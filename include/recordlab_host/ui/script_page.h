#pragma once

#include <QWidget>
#include <QStringList>

class QListWidget;
class QPlainTextEdit;

namespace recordlab::host::ui {

class SensorWorkspaceWidget;

class ScriptPage : public QWidget {
    Q_OBJECT

public:
    explicit ScriptPage(QWidget* parent = nullptr);

    SensorWorkspaceWidget* sensorWorkspace() const;
    QListWidget* scriptList() const;
    QPlainTextEdit* logView() const;
    void setScripts(const QStringList& scripts);

signals:
    void runScriptRequested(const QString& script_path);
    void stopScriptRequested();

private:
    SensorWorkspaceWidget* sensor_workspace_ = nullptr;
    QListWidget* script_list_ = nullptr;
    QPlainTextEdit* log_view_ = nullptr;
};

}  // namespace recordlab::host::ui
