#pragma once

#include "recordlab_host/bus/host_message_bus.h"

#include <QObject>
#include <QProcess>
#include <QTimer>

#include <memory>
#include <string>
#include <unordered_map>

namespace recordlab::host {

/// ScriptsActuator (PLAN.md T5) — manages Python experiment script processes.
/// Lives in the Qt thread (needs QProcess). Polls the bus for script requests
/// and publishes results back to the bus.
class ScriptsActuator : public QObject {
    Q_OBJECT

public:
    ScriptsActuator(HostMessageBus& bus, QString nodes_root,
                    QString echo_python_root, QString agents_config_path,
                    QString python_bin, QObject* parent = nullptr);
    ~ScriptsActuator() override;

    ScriptsActuator(const ScriptsActuator&) = delete;
    ScriptsActuator& operator=(const ScriptsActuator&) = delete;

private:
    void pollBus();
    void doRunScript(const std::string& script_path, const std::string& agent_name);
    void doStopScript();
    QString resolveScriptPath(const QString& script_path) const;
    void processOutputBytes(const QByteArray& data, QByteArray& buffer);
    void processOutputLine(const QString& line, const std::string& stream);
    bool handleRuntimeEvent(const QString& line);
    void handleDialogEvent(const nlohmann::json& event);
    void handleCommandRequestEvent(const nlohmann::json& event);
    void handleWatchdogStateRequestEvent(const nlohmann::json& event);
    void handleWatchdogEnsureRequestEvent(const nlohmann::json& event);
    void handleCreateDirectoryEvent(const nlohmann::json& event);
    void handleDialogResponse(const nlohmann::json& payload);
    void handleCommandResult(const nlohmann::json& payload);
    void handleWatchdogStateBusEvent(const nlohmann::json& payload);
    void handleWorkflowEvent(const nlohmann::json& event);
    void sendRuntimeResponse(const nlohmann::json& response);
    void publishToUI(const std::string& type, nlohmann::json payload);
    void publishLog(std::string message,
                    std::string level = "info",
                    std::string log_type = "script",
                    nlohmann::json extra = nlohmann::json::object());
    void reportException(const char* context, const std::exception* error = nullptr);

    HostMessageBus& bus_;
    QString nodes_root_;
    QString echo_python_root_;
    QString agents_config_path_;
    QString python_bin_;
    std::unique_ptr<QProcess> script_process_;
    QString current_agent_;
    bool stop_requested_ = false;
    QByteArray stdout_buffer_;
    QByteArray stderr_buffer_;
    QTimer* poll_timer_ = nullptr;
    std::string current_script_id_;
    std::string current_script_path_;
    std::string current_agent_name_;
    long long current_script_pid_ = 0;
    std::unordered_map<std::string, nlohmann::json> latest_watchdog_state_by_agent_;
};

}  // namespace recordlab::host
