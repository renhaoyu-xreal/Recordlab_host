#include "recordlab_host/scripts/scripts_actuator.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QUuid>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <functional>

namespace recordlab::host {

namespace {

QString jsonStringValue(const nlohmann::json& object, const char* key, const QString& fallback = {}) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (it->is_string()) {
        return QString::fromStdString(it->get<std::string>());
    }
    if (it->is_boolean()) {
        return *it ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (it->is_number_integer() || it->is_number_unsigned()) {
        return QString::number(it->get<long long>());
    }
    if (it->is_number_float()) {
        return QString::number(it->get<double>());
    }
    return QString::fromStdString(it->dump());
}

QStringList jsonStringList(const nlohmann::json& value) {
    QStringList items;
    if (!value.is_array()) {
        return items;
    }
    for (const auto& item : value) {
        if (item.is_string()) {
            items << QString::fromStdString(item.get<std::string>());
        } else if (!item.is_null()) {
            items << QString::fromStdString(item.dump());
        }
    }
    return items;
}

QStringList jsonStringListValue(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) {
        return {};
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        return {};
    }
    return jsonStringList(*it);
}

int jsonTimeoutMs(const nlohmann::json& object) {
    const auto timeout_ms = object.find("timeout_ms");
    if (timeout_ms != object.end() && timeout_ms->is_number()) {
        return static_cast<int>(timeout_ms->get<double>());
    }
    const auto timeout_s = object.find("timeout_s");
    if (timeout_s != object.end() && timeout_s->is_number()) {
        return static_cast<int>(timeout_s->get<double>() * 1000.0);
    }
    return 0;
}

nlohmann::json makeLogPayload(std::string message,
                              std::string level,
                              std::string log_type,
                              nlohmann::json extra = nlohmann::json::object()) {
    extra["message"] = std::move(message);
    extra["level"] = std::move(level);
    extra["log_type"] = std::move(log_type);
    return extra;
}

bool isUrAgentName(const std::string& agent_name) {
    std::string normalized = agent_name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized == "ur_node" || normalized.rfind("ur_", 0) == 0;
}

bool scriptReferencesUrAgent(const std::string& script_path) {
    if (script_path.empty()) {
        return false;
    }
    std::ifstream in(script_path);
    if (!in.is_open()) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text.find("UR_node") != std::string::npos || text.find("ur_node") != std::string::npos;
}

nlohmann::json workflowStepsFromPayload(const nlohmann::json& payload) {
    if (payload.is_object()) {
        const auto steps_it = payload.find("steps");
        if (steps_it != payload.end() && steps_it->is_array()) {
            return *steps_it;
        }
        const auto steps_json_it = payload.find("steps_json");
        if (steps_json_it != payload.end() && steps_json_it->is_string()) {
            try {
                const auto parsed = nlohmann::json::parse(steps_json_it->get<std::string>());
                if (parsed.is_array()) {
                    return parsed;
                }
            } catch (...) {
            }
        }
    }
    return nlohmann::json::array();
}

nlohmann::json buildStopWorkflowPayload(nlohmann::json payload, bool finished, bool graceful) {
    if (!payload.is_object()) {
        payload = nlohmann::json::object();
    }
    auto steps = workflowStepsFromPayload(payload);
    if (!steps.is_array()) {
        steps = nlohmann::json::array();
    }
    if (steps.empty()) {
        steps.push_back({
            {"label", "停止执行"},
            {"status", finished ? "stopped" : "stopping"},
        });
    }

    const std::string running_message = "用户停止执行，正在停止机械臂并收尾";
    const std::string stopped_message = graceful
        ? "用户停止执行，已停止并完成收尾"
        : "用户停止执行，机械臂已请求急停，脚本已中断";

    bool updated = false;
    if (!finished) {
        for (auto& step : steps) {
            if (!step.is_object()) {
                continue;
            }
            const auto status = step.value("status", std::string{});
            if (status == "running") {
                step["status"] = "stopping";
                step["message"] = running_message;
                updated = true;
            }
        }
        if (!updated) {
            for (auto& step : steps) {
                if (!step.is_object()) {
                    continue;
                }
                const auto status = step.value("status", std::string{});
                if (status == "pending") {
                    step["status"] = "stopping";
                    step["message"] = running_message;
                    updated = true;
                    break;
                }
            }
        }
        if (!updated) {
            for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
                if (!it->is_object()) {
                    continue;
                }
                (*it)["status"] = "stopping";
                (*it)["message"] = running_message;
                updated = true;
                break;
            }
        }
    } else {
        for (auto& step : steps) {
            if (!step.is_object()) {
                continue;
            }
            const auto status = step.value("status", std::string{});
            if (status == "stopping" || status == "running") {
                step["status"] = "stopped";
                step["message"] = stopped_message;
                updated = true;
            }
        }
        if (!updated) {
            for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
                if (!it->is_object()) {
                    continue;
                }
                const auto status = it->value("status", std::string{});
                if (status != "success") {
                    (*it)["status"] = "stopped";
                    (*it)["message"] = stopped_message;
                    updated = true;
                    break;
                }
            }
        }
        if (!updated && !steps.empty() && steps.back().is_object()) {
            steps.back()["status"] = "stopped";
            steps.back()["message"] = stopped_message;
        }
    }

    payload["action"] = "state";
    if (payload.find("title") == payload.end() || !payload["title"].is_string()) {
        payload["title"] = "脚本流程";
    }
    payload["message"] = finished ? stopped_message : running_message;
    payload["finished"] = finished;
    payload["success"] = finished ? graceful : false;
    payload["steps"] = steps;
    payload["steps_json"] = steps.dump();
    return payload;
}

}  // namespace

ScriptsActuator::ScriptsActuator(HostMessageBus& bus, QString nodes_root,
                                 QString echo_python_root, QString agents_config_path,
                                 QString python_bin,
                                 QObject* parent)
    : QObject(parent), bus_(bus),
      nodes_root_(std::move(nodes_root)),
      echo_python_root_(std::move(echo_python_root)),
      agents_config_path_(std::move(agents_config_path)),
      python_bin_(std::move(python_bin)) {
    bus_.registerConsumer(msg::SCRIPTS_ACTUATOR);
    bus_.subscribe(msg::SCRIPTS_ACTUATOR, msg::WATCHDOG_STATE, msg::WATCHDOG);
    bus_.subscribe(msg::SCRIPTS_ACTUATOR, msg::NODE_COOKIES, "data_receiver");
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(50);
    connect(poll_timer_, &QTimer::timeout, this, [this]() {
        try {
            pollBus();
        } catch (const std::exception& e) {
            reportException("pollBus", &e);
        } catch (...) {
            reportException("pollBus");
        }
    });
    poll_timer_->start();
}

ScriptsActuator::~ScriptsActuator() {
    try {
        stopScriptProcess(true);
    } catch (...) {
    }
}

void ScriptsActuator::pollBus() {
    auto messages = bus_.drainFor(msg::SCRIPTS_ACTUATOR);
    for (const auto& m : messages) {
        if (m.type == msg::RUN_SCRIPT) {
            const auto path = m.payload.value("script_path", std::string{});
            const auto agent = m.payload.value("agent_name", std::string{});
            doRunScript(path, agent);
        } else if (m.type == msg::STOP_SCRIPT) {
            doStopScript();
        } else if (m.type == msg::UI_DIALOG_RESPONSE) {
            handleDialogResponse(m.payload);
        } else if (m.type == msg::CMD_RESULT) {
            handleCommandResult(m.payload);
        } else if (m.type == msg::WATCHDOG_STATE) {
            handleWatchdogStateBusEvent(m.payload);
        } else if (m.type == msg::NODE_COOKIES) {
            latest_node_cookie_values_ = nlohmann::json::object();
            const auto items = m.payload.value("cookies", nlohmann::json::array());
            if (items.is_array()) {
                for (const auto& item : items) {
                    if (!item.is_object()) {
                        continue;
                    }
                    const auto key_it = item.find("key");
                    const auto value_it = item.find("value");
                    if (key_it == item.end() || !key_it->is_string() || value_it == item.end()) {
                        continue;
                    }
                    latest_node_cookie_values_[key_it->get<std::string>()] = *value_it;
                }
            }
        }
    }
}

void ScriptsActuator::doRunScript(const std::string& script_path, const std::string& agent_name) {
    if (script_process_ && script_process_->state() != QProcess::NotRunning) {
        publishLog("已有脚本正在运行", "warning", "script");
        return;
    }
    const QString resolved = resolveScriptPath(QString::fromStdString(script_path));
    if (resolved.isEmpty()) {
        publishLog("没有选择脚本", "warning", "script");
        return;
    }
    current_agent_ = QString::fromStdString(agent_name);
    stop_requested_ = false;
    publishToUI(msg::SCRIPT_WORKFLOW, {{"action", "clear"}});

    current_script_id_ = "script_" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    current_script_path_ = resolved.toStdString();
    current_agent_name_ = agent_name;
    current_script_pid_ = 0;
    current_script_required_agents_.clear();
    last_workflow_payload_ = nlohmann::json::object();
    script_process_ = std::make_unique<QProcess>();
    script_process_->setWorkingDirectory(nodes_root_);
    script_process_->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"),
               nodes_root_ + QStringLiteral(":") + echo_python_root_ +
               QStringLiteral(":") + env.value(QStringLiteral("PYTHONPATH")));
    env.insert(QStringLiteral("RECORDLAB_AGENT"), current_agent_);
    env.insert(QStringLiteral("RECORDLAB_AGENTS_CONFIG"), agents_config_path_);
    env.insert(QStringLiteral("RECORDLAB_NODES_ROOT"), nodes_root_);
    env.insert(QStringLiteral("ECHO_MESSAGE_SYSTEM_PYTHON_ROOT"), echo_python_root_);
    env.insert(QStringLiteral("RECORDLAB_DATA_REGISTRY_HOST"), QString::fromLocal8Bit(qgetenv("RECORDLAB_DATA_REGISTRY_HOST")));
    env.insert(QStringLiteral("RECORDLAB_DATA_REGISTRY_PORT"), QString::fromLocal8Bit(qgetenv("RECORDLAB_DATA_REGISTRY_PORT")));
    env.insert(QStringLiteral("RECORDLAB_USE_HOST_BRIDGE"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    const std::string all_log_path = common::Logger::instance().allLogPath();
    if (!all_log_path.empty()) {
        env.insert(QStringLiteral("RECORDLAB_LOG_DIR"),
                   QFileInfo(QString::fromStdString(all_log_path)).dir().absolutePath());
    }
    script_process_->setProcessEnvironment(env);

    connect(script_process_.get(), &QProcess::readyReadStandardOutput, this, [this]() {
        try {
            processOutputBytes(script_process_->readAllStandardOutput(), stdout_buffer_);
        } catch (const std::exception& e) {
            reportException("readyReadStandardOutput", &e);
        } catch (...) {
            reportException("readyReadStandardOutput");
        }
    });
    connect(script_process_.get(), &QProcess::readyReadStandardError, this, [this]() {
        try {
            processOutputBytes(script_process_->readAllStandardError(), stderr_buffer_);
        } catch (const std::exception& e) {
            reportException("readyReadStandardError", &e);
        } catch (...) {
            reportException("readyReadStandardError");
        }
    });
    connect(script_process_.get(), &QProcess::started, this, [this]() {
        try {
            const auto pid = static_cast<long long>(script_process_->processId());
            current_script_pid_ = pid;
            publishToUI(msg::SCRIPT_STARTED, {
                {"script_id", current_script_id_},
                {"script_path", current_script_path_},
                {"agent_name", current_agent_name_},
                {"pid", pid},
            });
            common::Logger::instance().log(common::LogLevel::Info, "ScriptsActuator", "script started", {
                {"script_id", current_script_id_},
                {"script_path", current_script_path_},
                {"agent_name", current_agent_name_},
                {"pid", pid},
            });
        } catch (const std::exception& e) {
            reportException("process started", &e);
        } catch (...) {
            reportException("process started");
        }
    });
    connect(script_process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exit_code, QProcess::ExitStatus exit_status) {
        try {
            if (!stdout_buffer_.trimmed().isEmpty()) processOutputLine(QString::fromUtf8(stdout_buffer_).trimmed(), "stdout");
            if (!stderr_buffer_.trimmed().isEmpty()) processOutputLine(QString::fromUtf8(stderr_buffer_).trimmed(), "stderr");
            stdout_buffer_.clear();
            stderr_buffer_.clear();
            const bool graceful_stop = stop_requested_
                && exit_status == QProcess::NormalExit
                && (exit_code == 0 || exit_code == 130);
            if (stop_requested_) {
                publishStopWorkflowState(true, graceful_stop);
            }
            const auto pid = current_script_pid_;
            publishToUI(msg::SCRIPT_FINISHED, {
                {"script_id", current_script_id_},
                {"script_path", current_script_path_},
                {"pid", pid},
                {"exit_code", exit_code},
                {"stop_requested", stop_requested_},
            });
            publishLog(
                stop_requested_
                    ? (graceful_stop
                        ? "脚本已停止并完成收尾"
                        : "脚本已中断退出，机械臂已请求急停，退出码: " + std::to_string(exit_code))
                    : "脚本退出: " + std::to_string(exit_code),
                stop_requested_ ? (graceful_stop ? "success" : "warning") : (exit_code == 0 ? "success" : "error"),
                "script",
                {
                    {"process", "script"},
                    {"script_path", current_script_path_},
                    {"pid", pid},
                    {"script_id", current_script_id_},
                });
            common::Logger::instance().log(common::LogLevel::Info, "ScriptsActuator", "script finished", {
                {"script_id", current_script_id_},
                {"script_path", current_script_path_},
                {"pid", pid},
                {"exit_code", exit_code},
                {"stop_requested", stop_requested_},
            });
        } catch (const std::exception& e) {
            reportException("process finished", &e);
        } catch (...) {
            reportException("process finished");
        }
    });

    publishLog("运行脚本: " + resolved.toStdString(), "info", "script");
    const QString python_bin = env.value(QStringLiteral("RECORDLAB_PYTHON_BIN"), python_bin_);
    const QString runtime = QDir(nodes_root_).filePath(QStringLiteral("node_scripts/runtime/run_recordlab_script.py"));
    if (QFileInfo(runtime).exists()) {
        script_process_->start(python_bin, {
            runtime,
            QStringLiteral("--project-root"), nodes_root_,
            QStringLiteral("--config"), agents_config_path_,
            QStringLiteral("--script"), resolved,
        });
    } else {
        script_process_->start(python_bin, {resolved});
    }
}

void ScriptsActuator::doStopScript() {
    stopScriptProcess(false);
}

void ScriptsActuator::stopScriptProcess(bool wait_for_exit) {
    stop_requested_ = true;
    if (!script_process_ || script_process_->state() == QProcess::NotRunning) {
        return;
    }
    publishStopWorkflowState(false, false);
    requestEmergencyStopForScriptAgents();
    script_process_->terminate();
    if (!wait_for_exit) {
        QTimer::singleShot(25000, this, [this]() {
            try {
                if (script_process_ && script_process_->state() != QProcess::NotRunning) script_process_->kill();
            } catch (const std::exception& e) {
                reportException("kill script process", &e);
            } catch (...) {
                reportException("kill script process");
            }
        });
        publishLog("正在停止脚本，并请求机械臂急停", "warning", "script");
        return;
    }

    constexpr int kTerminateWaitMs = 3000;
    constexpr int kKillWaitMs = 2000;
    if (script_process_->waitForFinished(kTerminateWaitMs)) {
        return;
    }
    common::Logger::instance().log(
        common::LogLevel::Warn,
        "ScriptsActuator",
        "script did not exit after terminate; forcing kill",
        {
            {"script_id", current_script_id_},
            {"script_path", current_script_path_},
            {"pid", current_script_pid_},
        });
    script_process_->kill();
    if (!script_process_->waitForFinished(kKillWaitMs)) {
        common::Logger::instance().log(
            common::LogLevel::Error,
            "ScriptsActuator",
            "script still running after kill during shutdown",
            {
                {"script_id", current_script_id_},
                {"script_path", current_script_path_},
                {"pid", current_script_pid_},
            });
    }
}

QString ScriptsActuator::resolveScriptPath(const QString& script_path) const {
    if (script_path.trimmed().isEmpty()) return {};
    const QFileInfo direct(script_path);
    if (direct.exists()) return direct.absoluteFilePath();
    const QFileInfo in_scripts(QDir(nodes_root_).filePath(QStringLiteral("node_scripts/") + script_path));
    if (in_scripts.exists()) return in_scripts.absoluteFilePath();
    const QFileInfo in_legacy_scripts(QDir(nodes_root_).filePath(QStringLiteral("node_scripts/") + script_path));
    if (in_legacy_scripts.exists()) return in_legacy_scripts.absoluteFilePath();
    return script_path;
}

void ScriptsActuator::processOutputBytes(const QByteArray& data, QByteArray& buffer) {
    if (data.isEmpty()) return;
    buffer.append(data);
    while (true) {
        const int newline = buffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line_bytes = buffer.left(newline);
        buffer.remove(0, newline + 1);
        const QString line = QString::fromUtf8(line_bytes).trimmed();
        if (!line.isEmpty()) processOutputLine(line, &buffer == &stdout_buffer_ ? "stdout" : "stderr");
    }
}

void ScriptsActuator::processOutputLine(const QString& line, const std::string& stream) {
    if (handleRuntimeEvent(line)) return;
    const auto pid = current_script_pid_ > 0
        ? current_script_pid_
        : static_cast<long long>(script_process_ ? script_process_->processId() : 0);
    publishToUI(msg::SCRIPT_OUTPUT, {
        {"text", line.toStdString()},
        {"stream", stream},
        {"process", "script"},
        {"script_path", current_script_path_},
        {"pid", pid},
        {"script_id", current_script_id_},
    });
    common::Logger::instance().log(
        stream == "stderr" ? common::LogLevel::Warn : common::LogLevel::Info,
        "ScriptsActuator",
        line.toStdString(),
        {
            {"stream", stream},
            {"process", "script"},
            {"script_path", current_script_path_},
            {"pid", pid},
            {"script_id", current_script_id_},
        });
}

bool ScriptsActuator::handleRuntimeEvent(const QString& line) {
    constexpr const char* kPrefix = "__RECORDLAB_EVENT__ ";
    if (!line.startsWith(QString::fromUtf8(kPrefix))) return false;
    try {
        const auto event = nlohmann::json::parse(line.mid(QString::fromUtf8(kPrefix).size()).toStdString());
        const auto type = event.value("type", std::string{});
        if (type == "dialog") handleDialogEvent(event);
        if (type == "cmd_request") handleCommandRequestEvent(event);
        if (type == "host_state_request") handleHostStateRequestEvent(event);
        if (type == "watchdog_state_request") handleWatchdogStateRequestEvent(event);
        if (type == "watchdog_ensure_request") handleWatchdogEnsureRequestEvent(event);
        if (type == "create_directory") handleCreateDirectoryEvent(event);
        if (type == "workflow") handleWorkflowEvent(event);
        if (type == "required_agents") handleRequiredAgentsEvent(event);
    } catch (const std::exception& e) {
        publishLog(std::string("runtime 事件解析失败: ") + e.what(), "error", "script");
    } catch (...) {
        publishLog("runtime 事件解析失败: unknown exception", "error", "script");
    }
    return true;
}

void ScriptsActuator::handleDialogEvent(const nlohmann::json& event) {
    nlohmann::json payload = event;
    payload["dialog_id"] = event.value("id", event.value("dialog_id", std::string{}));
    bus_.publish({
        .source = msg::SCRIPTS_ACTUATOR,
        .target = msg::UI,
        .type = msg::UI_DIALOG_REQUEST,
        .payload = std::move(payload),
    });
}

void ScriptsActuator::handleCommandRequestEvent(const nlohmann::json& event) {
    const std::string request_id = event.value("id", event.value("request_id", std::string{}));
    const std::string cmd = event.value("cmd", std::string{});
    const int timeout_ms = jsonTimeoutMs(event);
    const std::string priority = event.value("priority", std::string("normal"));
    bus_.publish({
        .request_id = request_id,
        .source = msg::SCRIPTS_ACTUATOR,
        .target = priority == "high" ? msg::AGENT_MANAGER_PRIORITY : msg::AGENT_MANAGER,
        .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id},
            {"agent_name", event.value("agent_name", current_agent_name_)},
            {"cmd", cmd},
            {"params", event.value("params", nlohmann::json::object())},
            {"priority", priority},
            {"silent", event.value("silent", true)},
            {"timeout_ms", timeout_ms},
        },
    });
}

void ScriptsActuator::handleHostStateRequestEvent(const nlohmann::json& event) {
    const std::string request_id = event.value("id", event.value("request_id", std::string{}));
    const std::string state_key = event.value("state_key", std::string{});
    nlohmann::json response = nullptr;
    if (state_key == "node_cookie") {
        response = latest_node_cookie_values_;
    }
    sendRuntimeResponse({
        {"id", request_id},
        {"request_id", request_id},
        {"success", true},
        {"cancelled", false},
        {"response", response},
    });
}

void ScriptsActuator::handleWatchdogStateRequestEvent(const nlohmann::json& event) {
    const std::string request_id = event.value("id", event.value("request_id", std::string{}));
    std::string agent_name = event.value("agent_name", std::string{});
    if (agent_name.empty()) {
        agent_name = current_agent_name_;
    }

    nlohmann::json response = nullptr;
    if (!agent_name.empty()) {
        const auto it = latest_watchdog_state_by_agent_.find(agent_name);
        if (it != latest_watchdog_state_by_agent_.end()) {
            response = it->second;
        }
    }
    if (response.is_null() && !current_agent_name_.empty()) {
        const auto it = latest_watchdog_state_by_agent_.find(current_agent_name_);
        if (it != latest_watchdog_state_by_agent_.end()) {
            response = it->second;
        }
    }

    sendRuntimeResponse({
        {"id", request_id},
        {"request_id", request_id},
        {"success", true},
        {"cancelled", false},
        {"response", response},
    });
}

void ScriptsActuator::handleWatchdogEnsureRequestEvent(const nlohmann::json& event) {
    const std::string request_id = event.value("id", event.value("request_id", std::string{}));
    std::string agent_name = event.value("agent_name", std::string{});
    if (agent_name.empty()) {
        agent_name = current_agent_name_;
    }

    bus_.publish({
        .request_id = request_id,
        .source = msg::SCRIPTS_ACTUATOR,
        .target = msg::WATCHDOG_CONTROL,
        .type = msg::WATCHDOG_ENSURE_DEVICE,
        .payload = {
            {"request_id", request_id},
            {"agent_name", agent_name},
            {"source", std::string("script")},
        },
    });
    sendRuntimeResponse({
        {"id", request_id},
        {"request_id", request_id},
        {"success", true},
        {"cancelled", false},
        {"response", {{"requested", true}, {"agent_name", agent_name}}},
    });
}

void ScriptsActuator::handleCreateDirectoryEvent(const nlohmann::json& event) {
    const QString path = jsonStringValue(event, "path");
    const bool ok = !path.trimmed().isEmpty() && QDir().mkpath(path);
    sendRuntimeResponse({
        {"id", event.value("id", event.value("request_id", std::string{}))},
        {"success", ok},
        {"cancelled", false},
        {"response", path.toStdString()},
    });
}

void ScriptsActuator::handleDialogResponse(const nlohmann::json& payload) {
    nlohmann::json response = payload;
    if (!response.contains("id")) {
        response["id"] = response.value("dialog_id", std::string{});
    }
    sendRuntimeResponse(response);
}

void ScriptsActuator::handleCommandResult(const nlohmann::json& payload) {
    sendRuntimeResponse({
        {"id", payload.value("request_id", std::string{})},
        {"request_id", payload.value("request_id", std::string{})},
        {"success", payload.value("success", false)},
        {"cancelled", false},
        {"response", payload},
    });
}

void ScriptsActuator::handleWatchdogStateBusEvent(const nlohmann::json& payload) {
    const auto agent_name = payload.value("agent_name", std::string{});
    if (!agent_name.empty()) {
        latest_watchdog_state_by_agent_[agent_name] = payload;
    }
}

void ScriptsActuator::handleRequiredAgentsEvent(const nlohmann::json& event) {
    current_script_required_agents_.clear();
    const auto agent_names = event.find("agent_names");
    if (agent_names != event.end() && agent_names->is_array()) {
        for (const auto& item : *agent_names) {
            if (item.is_string()) {
                current_script_required_agents_.push_back(item.get<std::string>());
            }
        }
    }
    publishToUI(msg::SCRIPT_REQUIRED_AGENTS, event);
}

void ScriptsActuator::handleWorkflowEvent(const nlohmann::json& event) {
    nlohmann::json payload = event;
    if (payload.contains("steps") && payload["steps"].is_array()) {
        payload["steps_json"] = payload["steps"].dump();
    }
    last_workflow_payload_ = payload;
    publishToUI(msg::SCRIPT_WORKFLOW, std::move(payload));
}

void ScriptsActuator::publishStopWorkflowState(bool finished, bool graceful) {
    const auto payload = buildStopWorkflowPayload(last_workflow_payload_, finished, graceful);
    last_workflow_payload_ = payload;
    publishToUI(msg::SCRIPT_WORKFLOW, payload);
}

void ScriptsActuator::requestEmergencyStopForScriptAgents() {
    std::vector<std::string> target_agents;
    const auto add_target = [&target_agents](const std::string& agent_name) {
        if (!isUrAgentName(agent_name)) {
            return;
        }
        if (std::find(target_agents.begin(), target_agents.end(), agent_name) == target_agents.end()) {
            target_agents.push_back(agent_name);
        }
    };

    for (const auto& agent_name : current_script_required_agents_) {
        add_target(agent_name);
    }
    if (target_agents.empty()
        && (current_script_path_.find("record_ur_") != std::string::npos
            || scriptReferencesUrAgent(current_script_path_))) {
        add_target("UR_node");
    }

    for (const auto& agent_name : target_agents) {
        bus_.publish({
            .source = msg::SCRIPTS_ACTUATOR,
            .target = msg::AGENT_MANAGER_PRIORITY,
            .type = msg::ESTOP,
            .payload = {
                {"agent_name", agent_name},
            },
        });
        publishLog("已发起机械臂急停: " + agent_name, "warning", "command");
    }
}

void ScriptsActuator::sendRuntimeResponse(const nlohmann::json& response) {
    if (!script_process_ || script_process_->state() == QProcess::NotRunning) return;
    QByteArray bytes = QByteArray::fromStdString(response.dump());
    bytes.append('\n');
    script_process_->write(bytes);
    script_process_->waitForBytesWritten(1000);
}

void ScriptsActuator::publishToUI(const std::string& type, nlohmann::json payload) {
    bus_.publish({.source = msg::SCRIPTS_ACTUATOR, .target = msg::UI, .type = type, .payload = std::move(payload)});
}

void ScriptsActuator::publishLog(std::string message,
                                 std::string level,
                                 std::string log_type,
                                 nlohmann::json extra) {
    publishToUI(msg::LOG_ENTRY, makeLogPayload(
        std::move(message), std::move(level), std::move(log_type), std::move(extra)));
}

void ScriptsActuator::reportException(const char* context, const std::exception* error) {
    const std::string detail = error ? error->what() : "unknown exception";
    const std::string message = std::string("脚本运行器内部异常(") + context + "): " + detail;
    try {
        publishLog(message, "error", "script");
    } catch (...) {
    }
    try {
        common::Logger::instance().log(common::LogLevel::Error, "ScriptsActuator", message);
    } catch (...) {
    }
}

}  // namespace recordlab::host
