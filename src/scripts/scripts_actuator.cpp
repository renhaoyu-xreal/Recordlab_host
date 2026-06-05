#include "recordlab_host/scripts/scripts_actuator.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QComboBox>
#include <QVBoxLayout>
#include <QUuid>

namespace recordlab::host {

ScriptsActuator::ScriptsActuator(HostMessageBus& bus, QString nodes_root,
                                 QString echo_python_root, QString agents_config_path,
                                 QObject* parent)
    : QObject(parent), bus_(bus),
      nodes_root_(std::move(nodes_root)),
      echo_python_root_(std::move(echo_python_root)),
      agents_config_path_(std::move(agents_config_path)) {
    bus_.registerConsumer(msg::SCRIPTS_ACTUATOR);
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(50);
    connect(poll_timer_, &QTimer::timeout, this, &ScriptsActuator::pollBus);
    poll_timer_->start();
}

ScriptsActuator::~ScriptsActuator() {
    doStopScript();
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
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    const std::string all_log_path = common::Logger::instance().allLogPath();
    if (!all_log_path.empty()) {
        env.insert(QStringLiteral("RECORDLAB_LOG_DIR"),
                   QFileInfo(QString::fromStdString(all_log_path)).dir().absolutePath());
    }
    script_process_->setProcessEnvironment(env);

    connect(script_process_.get(), &QProcess::readyReadStandardOutput, this, [this]() {
        processOutputBytes(script_process_->readAllStandardOutput(), stdout_buffer_);
    });
    connect(script_process_.get(), &QProcess::readyReadStandardError, this, [this]() {
        processOutputBytes(script_process_->readAllStandardError(), stderr_buffer_);
    });
    connect(script_process_.get(), &QProcess::started, this, [this]() {
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
    });
    connect(script_process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exit_code, QProcess::ExitStatus exit_status) {
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
    });

    publishToUI(msg::LOG_ENTRY, {{"message", "运行脚本: " + resolved.toStdString()}});
    const QString python_bin = env.value(QStringLiteral("RECORDLAB_PYTHON_BIN"), QStringLiteral("python3.10"));
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
            if (script_process_ && script_process_->state() != QProcess::NotRunning) script_process_->kill();
        });
        publishToUI(msg::LOG_ENTRY, {{"message", "脚本已停止"}});
    }
}

QString ScriptsActuator::resolveScriptPath(const QString& script_path) const {
    if (script_path.trimmed().isEmpty()) return {};
    const QFileInfo direct(script_path);
    if (direct.exists()) return direct.absoluteFilePath();
    const QFileInfo in_legacy_scripts(QDir(nodes_root_).filePath(QStringLiteral("scripts/") + script_path));
    if (in_legacy_scripts.exists()) return in_legacy_scripts.absoluteFilePath();
    const QFileInfo in_scripts(QDir(nodes_root_).filePath(QStringLiteral("node_scripts/") + script_path));
    if (in_scripts.exists()) return in_scripts.absoluteFilePath();
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
    publishToUI(msg::LOG_ENTRY, {
        {"message", line.toStdString()},
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
        if (type == "workflow") handleWorkflowEvent(event);
    } catch (const std::exception& e) {
        publishToUI(msg::LOG_ENTRY, {{"message", std::string("runtime 事件解析失败: ") + e.what()}});
    }
    return true;
}

void ScriptsActuator::handleDialogEvent(const nlohmann::json& event) {
    const QString id = QString::fromStdString(event.value("id", std::string{}));
    const QString kind = QString::fromStdString(event.value("kind", std::string("info")));
    const QString title = QString::fromStdString(event.value("title", std::string("脚本提示")));
    const QString message = QString::fromStdString(event.value("message", std::string{}));
    nlohmann::json response = {{"id", id.toStdString()}, {"success", true}, {"cancelled", false}};

    if (kind == QStringLiteral("question")) {
        response["response"] = QMessageBox::question(nullptr, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
    } else if (kind == QStringLiteral("input")) {
        bool ok = false;
        const QString value = QInputDialog::getText(nullptr, title, message, QLineEdit::Normal,
                                                    QString::fromStdString(event.value("default", std::string{})), &ok);
        response["cancelled"] = !ok;
        response["response"] = value.toStdString();
    } else if (kind == QStringLiteral("multi_field_input")) {
        QDialog dialog;
        dialog.setWindowTitle(title);
        dialog.resize(520, 320);
        auto* layout = new QVBoxLayout(&dialog);
        auto* label = new QLabel(message, &dialog);
        label->setWordWrap(true);
        layout->addWidget(label);
        auto* form = new QFormLayout();
        struct Input { QString name; QLineEdit* edit = nullptr; QComboBox* combo = nullptr; };
        std::vector<Input> inputs;
        for (const auto& field : event.value("fields", nlohmann::json::array())) {
            const QString name = QString::fromStdString(field.value("name", std::string{}));
            const QString field_label = QString::fromStdString(field.value("label", name.toStdString()));
            QStringList choices;
            const auto raw_choices = field.contains("choices") ? field["choices"] : field.value("options", nlohmann::json::array());
            for (const auto& choice : raw_choices) if (choice.is_string()) choices << QString::fromStdString(choice.get<std::string>());
            if (!choices.isEmpty()) {
                auto* combo = new QComboBox(&dialog);
                combo->addItems(choices);
                const int idx = combo->findText(QString::fromStdString(field.value("default", std::string{})));
                if (idx >= 0) combo->setCurrentIndex(idx);
                form->addRow(field_label, combo);
                inputs.push_back({name, nullptr, combo});
            } else {
                auto* edit = new QLineEdit(QString::fromStdString(field.value("default", std::string{})), &dialog);
                form->addRow(field_label, edit);
                inputs.push_back({name, edit, nullptr});
            }
        }
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (dialog.exec() == QDialog::Accepted) {
            nlohmann::json values = nlohmann::json::object();
            for (const auto& input : inputs) {
                values[input.name.toStdString()] = input.combo ? input.combo->currentText().toStdString() : input.edit->text().toStdString();
            }
            response["response"] = values;
        } else {
            response["cancelled"] = true;
        }
    } else {
        if (kind == QStringLiteral("error")) QMessageBox::critical(nullptr, title, message);
        else if (kind == QStringLiteral("warning")) QMessageBox::warning(nullptr, title, message);
        else QMessageBox::information(nullptr, title, message);
        response["response"] = true;
    }
    sendRuntimeResponse(response);
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

}  // namespace recordlab::host
