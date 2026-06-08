#include "recordlab_host/scripts/scripts_actuator.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QUuid>

#include <exception>
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
        doStopScript();
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
        }
    }
}

void ScriptsActuator::doRunScript(const std::string& script_path, const std::string& agent_name) {
    if (script_process_ && script_process_->state() != QProcess::NotRunning) {
        publishToUI(msg::LOG_ENTRY, {{"message", "已有脚本正在运行"}});
        return;
    }
    const QString resolved = resolveScriptPath(QString::fromStdString(script_path));
    if (resolved.isEmpty()) {
        publishToUI(msg::LOG_ENTRY, {{"message", "没有选择脚本"}});
        return;
    }
    current_agent_ = QString::fromStdString(agent_name);
    stop_requested_ = false;
    publishToUI(msg::SCRIPT_WORKFLOW, {{"action", "clear"}});

    current_script_id_ = "script_" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    current_script_path_ = resolved.toStdString();
    current_agent_name_ = agent_name;
    current_script_pid_ = 0;
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
            (void)exit_status;
            const auto pid = current_script_pid_;
            publishToUI(msg::SCRIPT_FINISHED, {
                {"script_id", current_script_id_},
                {"script_path", current_script_path_},
                {"pid", pid},
                {"exit_code", exit_code},
            });
            publishToUI(msg::LOG_ENTRY, {
                {"message", "脚本退出: " + std::to_string(exit_code)},
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
            });
        } catch (const std::exception& e) {
            reportException("process finished", &e);
        } catch (...) {
            reportException("process finished");
        }
    });

    publishToUI(msg::LOG_ENTRY, {{"message", "运行脚本: " + resolved.toStdString()}});
    const QString python_bin = env.value(QStringLiteral("RECORDLAB_PYTHON_BIN"), python_bin_);
    const QString runtime = QDir(nodes_root_).filePath(QStringLiteral("scripts/runtime/run_recordlab_script.py"));
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
    stop_requested_ = true;
    if (script_process_ && script_process_->state() != QProcess::NotRunning) {
        script_process_->terminate();
        QTimer::singleShot(25000, this, [this]() {
            try {
                if (script_process_ && script_process_->state() != QProcess::NotRunning) script_process_->kill();
            } catch (const std::exception& e) {
                reportException("kill script process", &e);
            } catch (...) {
                reportException("kill script process");
            }
        });
        publishToUI(msg::LOG_ENTRY, {{"message", "脚本已停止"}});
    }
}

QString ScriptsActuator::resolveScriptPath(const QString& script_path) const {
    if (script_path.trimmed().isEmpty()) return {};
    const QFileInfo direct(script_path);
    if (direct.exists()) return direct.absoluteFilePath();
    const QFileInfo in_scripts(QDir(nodes_root_).filePath(QStringLiteral("node_scripts/") + script_path));
    if (in_scripts.exists()) return in_scripts.absoluteFilePath();
    const QFileInfo in_legacy_scripts(QDir(nodes_root_).filePath(QStringLiteral("scripts/") + script_path));
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
        if (type == "create_directory") handleCreateDirectoryEvent(event);
        if (type == "workflow") handleWorkflowEvent(event);
        if (type == "required_agents") publishToUI(msg::SCRIPT_REQUIRED_AGENTS, event);
    } catch (const std::exception& e) {
        publishToUI(msg::LOG_ENTRY, {{"message", std::string("runtime 事件解析失败: ") + e.what()}});
    } catch (...) {
        publishToUI(msg::LOG_ENTRY, {{"message", "runtime 事件解析失败: unknown exception"}});
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
    bus_.publish({
        .request_id = request_id,
        .source = msg::SCRIPTS_ACTUATOR,
        .target = msg::AGENT_MANAGER,
        .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id},
            {"agent_name", event.value("agent_name", current_agent_name_)},
            {"cmd", cmd},
            {"params", event.value("params", nlohmann::json::object())},
            {"priority", event.value("priority", std::string("normal"))},
            {"silent", event.value("silent", true)},
            {"timeout_ms", timeout_ms},
        },
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

void ScriptsActuator::handleWorkflowEvent(const nlohmann::json& event) {
    nlohmann::json payload = event;
    if (payload.contains("steps") && payload["steps"].is_array()) {
        payload["steps_json"] = payload["steps"].dump();
    }
    publishToUI(msg::SCRIPT_WORKFLOW, std::move(payload));
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

void ScriptsActuator::reportException(const char* context, const std::exception* error) {
    const std::string detail = error ? error->what() : "unknown exception";
    const std::string message = std::string("脚本运行器内部异常(") + context + "): " + detail;
    try {
        publishToUI(msg::LOG_ENTRY, {{"message", message}});
    } catch (...) {
    }
    try {
        common::Logger::instance().log(common::LogLevel::Error, "ScriptsActuator", message);
    } catch (...) {
    }
}

}  // namespace recordlab::host
