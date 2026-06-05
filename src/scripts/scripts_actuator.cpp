#include "recordlab_host/scripts/scripts_actuator.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QComboBox>
#include <QVBoxLayout>
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

}  // namespace

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
    } catch (...) {
        publishToUI(msg::LOG_ENTRY, {{"message", "runtime 事件解析失败: unknown exception"}});
    }
    return true;
}

void ScriptsActuator::handleDialogEvent(const nlohmann::json& event) {
    const QString id = jsonStringValue(event, "id");
    const QString kind = jsonStringValue(event, "kind", QStringLiteral("info"));
    const QString title = jsonStringValue(event, "title", QStringLiteral("脚本提示"));
    const QString message = jsonStringValue(event, "message");
    nlohmann::json response = {{"id", id.toStdString()}, {"success", true}, {"cancelled", false}};

    if (kind == QStringLiteral("question")) {
        response["response"] = QMessageBox::question(nullptr, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
    } else if (kind == QStringLiteral("input")) {
        bool ok = false;
        const QString value = QInputDialog::getText(nullptr, title, message, QLineEdit::Normal,
                                                    jsonStringValue(event, "default"), &ok);
        response["cancelled"] = !ok;
        response["response"] = value.toStdString();
    } else if (kind == QStringLiteral("choice")) {
        const QStringList items = jsonStringListValue(event, "items");
        const bool multi_select = event.value("multi_select", false);
        if (multi_select) {
            QDialog dialog;
            dialog.setWindowTitle(title);
            dialog.resize(460, 360);
            auto* layout = new QVBoxLayout(&dialog);
            auto* label = new QLabel(message, &dialog);
            label->setWordWrap(true);
            layout->addWidget(label);
            auto* list = new QListWidget(&dialog);
            list->setSelectionMode(QAbstractItemView::MultiSelection);
            list->addItems(items);
            const QStringList defaults = jsonStringListValue(event, "default_selected");
            for (int i = 0; i < list->count(); ++i) {
                auto* item = list->item(i);
                if (defaults.contains(item->text())) item->setSelected(true);
            }
            layout->addWidget(list, 1);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            if (dialog.exec() == QDialog::Accepted) {
                nlohmann::json selected = nlohmann::json::array();
                for (auto* item : list->selectedItems()) selected.push_back(item->text().toStdString());
                response["response"] = selected;
            } else {
                response["cancelled"] = true;
            }
        } else {
            bool ok = false;
            const QString selected = QInputDialog::getItem(nullptr, title, message, items, 0, false, &ok);
            response["cancelled"] = !ok;
            response["response"] = selected.toStdString();
        }
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
        const auto fields_it = event.find("fields");
        const nlohmann::json fields = (fields_it != event.end() && fields_it->is_array())
            ? *fields_it
            : nlohmann::json::array();
        for (const auto& field : fields) {
            if (!field.is_object()) continue;
            const QString name = jsonStringValue(field, "name");
            const QString field_label = jsonStringValue(field, "label", name);
            QStringList choices;
            const auto choices_it = field.find("choices");
            const auto options_it = field.find("options");
            const nlohmann::json raw_choices =
                (choices_it != field.end() && choices_it->is_array()) ? *choices_it :
                (options_it != field.end() && options_it->is_array()) ? *options_it :
                nlohmann::json::array();
            choices = jsonStringList(raw_choices);
            if (!choices.isEmpty()) {
                auto* combo = new QComboBox(&dialog);
                combo->addItems(choices);
                const int idx = combo->findText(jsonStringValue(field, "default"));
                if (idx >= 0) combo->setCurrentIndex(idx);
                form->addRow(field_label, combo);
                inputs.push_back({name, nullptr, combo});
            } else {
                auto* edit = new QLineEdit(jsonStringValue(field, "default"), &dialog);
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
