#pragma once

#include "recordlab_host/agents/agent_manager.h"
#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/data/data_receiver.h"
#include "recordlab_host/lifecycle/watchdog.h"
#include "recordlab_host/scripts/scripts_actuator.h"

#include <QMainWindow>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <string>

class QStackedWidget;

namespace recordlab::host::ui {

class EntryPage;
class WorkspacePage;

/// MainWindow (PLAN.md T0) — owns the HostMessageBus, AgentManager, DataReceiver and
/// ScriptsActuator.  Polls the bus at ~30 Hz and converts messages into Qt signals
/// for the UI widgets.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::string agents_config_path,
                        std::string nodes_root,
                        std::string echo_python_root,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    EntryPage* entryPage() const;
    WorkspacePage* workspacePage() const;

    // Public for test accessibility.
    void pollBusMessages();

signals:
    void logMessage(const QString& message);
    void watchdogStateChanged(const QString& state);
    void commandFinished(const QString& cmd, bool success, const QString& message);
    void recordTimerChanged(double seconds);
    void timeDelayChanged(double milliseconds);
    void scriptFinished(int exit_code);

public slots:
    void sendCommand(const QString& cmd, const QString& params_json);
    void runScript(const QString& script_path);
    void stopScript();
    void activateAgent(const QString& agent_name);
    void shutdown();

private:
    void loadAgents();
    void handleUIMessage(const HostMessage& msg);
    void appendLog(const QString& message);
    void reportQtException(const QString& context, const std::exception* error = nullptr);

    // ── Paths / config ─────────────────────────────────────────
    std::string agents_config_path_;
    QString nodes_root_;
    QString echo_python_root_;
    QString active_agent_;

    // ── Architecture components (PLAN.md) ──────────────────────
    HostMessageBus bus_;
    std::unique_ptr<AgentManager> agent_manager_;
    std::unique_ptr<Watchdog> watchdog_;
    std::unique_ptr<DataReceiver> data_receiver_;
    std::unique_ptr<ScriptsActuator> scripts_actuator_;
    QTimer* bus_poll_timer_ = nullptr;

    // ── UI ─────────────────────────────────────────────────────
    QStackedWidget* stack_ = nullptr;
    EntryPage* entry_page_ = nullptr;
    WorkspacePage* workspace_page_ = nullptr;
};

}  // namespace recordlab::host::ui
