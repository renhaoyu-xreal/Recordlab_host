#pragma once

#include <QWidget>

#include <nlohmann/json.hpp>

#include <string>

class QLabel;
class QTabWidget;

namespace recordlab::host::ui {

class DataPage;
class MainWindow;
class ScriptPage;

class WorkspacePage : public QWidget {
    Q_OBJECT

public:
    explicit WorkspacePage(QWidget* parent = nullptr);

    void activateAgent(const QString& agent_name);
    QString activeAgent() const;
    QTabWidget* tabWidget() const;
    ScriptPage* scriptPage() const;
    DataPage* dataPage() const;
    void bindMainWindow(MainWindow* mainWindow);
    void handleTopicData(const QString& name, const nlohmann::json& value, double frequency);

signals:
    void backRequested();

private:
    void updateHeader();

    QString active_agent_;
    QLabel* timer_value_label_ = nullptr;
    QLabel* delay_value_label_ = nullptr;
    QLabel* watchdog_value_label_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    ScriptPage* script_page_ = nullptr;
    DataPage* data_page_ = nullptr;
    MainWindow* main_window_ = nullptr;
    bool saw_imu_data_ = false;
};

}  // namespace recordlab::host::ui
