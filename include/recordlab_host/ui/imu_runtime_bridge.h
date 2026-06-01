#pragma once

#include "recordlab_host/agents/agent_manager.h"
#include "recordlab_host/bus/host_message_bus.h"
#include "recordlab_host/common/logger.h"
#include "recordlab_host/data/data_receiver.h"
#include "recordlab_host/scripts/scripts_actuator.h"

#include <QObject>
#include <QTimer>

#include <memory>
#include <string>
#include <vector>

namespace recordlab::host::ui {

/// ImuRuntimeBridge — thin Qt adapter layer.
/// Delegates actual work to AgentManager, DataReceiver and ScriptsActuator
/// via the HostMessageBus.  Polls the bus and converts messages into Qt signals
/// consumed by the existing UI widgets.
class ImuRuntimeBridge : public QObject {
    Q_OBJECT

public:
    explicit ImuRuntimeBridge(std::string agents_config_path, QObject* parent = nullptr);
    ~ImuRuntimeBridge() override;

    std::vector<std::string> primaryAgents() const;
    std::vector<std::string> defaultScripts(const QString& agent_name) const;
    QString nodesRoot() const;
    QString nodeScriptsRoot() const;

public slots:
    void activateAgent(const QString& agent_name);
    void sendCommand(const QString& cmd, const QString& params_json);
    void runScript(const QString& script_path);
    void stopScript();
    void shutdown();

signals:
    void logMessage(const QString& message);
    void watchdogStateChanged(const QString& state);
    void commandFinished(const QString& cmd, bool success, const QString& message);
    void topicDataReceived(const QString& data_name, const QString& value_json, double frequency);
    void recordTimerChanged(double seconds);
    void timeDelayChanged(double milliseconds);
    void scriptFinished(int exit_code);

private:
    void setupDataReceiver(const AgentConfig& config);
    void pollBusMessages();
    void handleUIMessage(const HostMessage& msg);
    void appendLog(const QString& message);

    std::string agents_config_path_;
    QString nodes_root_;
    QString echo_python_root_;
    QString host_root_;
    QString log_dir_path_;
    QString log_ui_path_;
    QString log_all_path_;
    QString active_agent_;

    // Architecture components (PLAN.md)
    HostMessageBus bus_;
    std::unique_ptr<AgentManager> agent_manager_;
    std::unique_ptr<DataReceiver> data_receiver_;
    std::unique_ptr<ScriptsActuator> scripts_actuator_;
    QTimer* bus_poll_timer_ = nullptr;
};

}  // namespace recordlab::host::ui
