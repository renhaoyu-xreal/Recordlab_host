#pragma once

#include <QSet>
#include <QWidget>

#include <nlohmann/json.hpp>

#include <string>

class QLabel;
class QTabWidget;

namespace recordlab::host::ui {

class DataPage;
class MainWindow;
class RecordTimerDisplayWidget;
class ScriptPage;
class TimeDelayDisplayWidget;
class VirtualNodesPage;

class WorkspacePage : public QWidget {
    Q_OBJECT

public:
    explicit WorkspacePage(QWidget* parent = nullptr);

    void activateAgent(const QString& agent_name);
    QString activeAgent() const;
    QTabWidget* tabWidget() const;
    ScriptPage* scriptPage() const;
    DataPage* dataPage() const;
    VirtualNodesPage* virtualNodesPage() const;
    void bindMainWindow(MainWindow* mainWindow);
    void handleTopicData(const QString& name, const nlohmann::json& value, double frequency);
    void configureSensorLayout(const nlohmann::json& sensor_layout);

signals:
    void backRequested();

private:
    void updateHeader();

    QString active_agent_;
    RecordTimerDisplayWidget* timer_display_ = nullptr;
    TimeDelayDisplayWidget* delay_display_ = nullptr;
    QLabel* watchdog_value_label_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    ScriptPage* script_page_ = nullptr;
    DataPage* data_page_ = nullptr;
    VirtualNodesPage* virtual_nodes_page_ = nullptr;
    MainWindow* main_window_ = nullptr;
    QSet<QString> active_tab_only_topics_;
};

}  // namespace recordlab::host::ui
