#include "recordlab_host/ui/main_window.h"

#include "recordlab_host/agents/agent_config_loader.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"
#include "recordlab_host/ui/data_page.h"
#include "recordlab_host/ui/entry_page.h"
#include "recordlab_host/ui/script_page.h"
#include "recordlab_host/ui/workspace_page.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUuid>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <exception>
#include <cstdlib>

namespace recordlab::host::ui {

namespace {

bool isCheckCommand(const std::string& cmd) {
    return cmd == "check";
}

bool isCheckLogMessage(const QString& message) {
    const QString trimmed = message.trimmed();
    return trimmed.startsWith(QStringLiteral("发送命令: check "))
        || trimmed == QStringLiteral("发送命令: check")
        || trimmed.startsWith(QStringLiteral("check:"))
        || trimmed.startsWith(QStringLiteral("check 失败:"));
}

QString replaceTemplateVars(QString text, const nlohmann::json& vars) {
    if (!vars.is_object()) return text;
    for (const auto& [key, value] : vars.items()) {
        QString replacement;
        if (value.is_string()) replacement = QString::fromStdString(value.get<std::string>());
        else if (value.is_number_float()) replacement = QString::number(value.get<double>());
        else if (value.is_number_integer()) replacement = QString::number(value.get<long long>());
        else if (value.is_boolean()) replacement = value.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
        else replacement = QString::fromStdString(value.dump());
        text.replace(QStringLiteral("${%1}").arg(QString::fromStdString(key)), replacement);
    }
    return text;
}

QString jsonToQString(const nlohmann::json& value, const QString& fallback = {}) {
    if (value.is_string()) return QString::fromStdString(value.get<std::string>());
    if (value.is_boolean()) return value.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.is_number_integer()) return QString::number(value.get<long long>());
    if (value.is_number_unsigned()) return QString::number(value.get<unsigned long long>());
    if (value.is_number_float()) return QString::number(value.get<double>());
    if (!value.is_null()) return QString::fromStdString(value.dump());
    return fallback;
}

bool jsonBoolValue(const nlohmann::json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) return fallback;
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return fallback;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number_integer()) return it->get<long long>() != 0;
    if (it->is_number_unsigned()) return it->get<unsigned long long>() != 0;
    if (it->is_string()) {
        const auto value = QString::fromStdString(it->get<std::string>()).trimmed().toLower();
        if (value == QStringLiteral("true") || value == QStringLiteral("1")) return true;
        if (value == QStringLiteral("false") || value == QStringLiteral("0")) return false;
    }
    return fallback;
}

nlohmann::json showMultiFieldInputDialog(QWidget* parent,
                                         const QString& title,
                                         const QString& message,
                                         const nlohmann::json& fields,
                                         bool* accepted) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    if (!message.trimmed().isEmpty()) {
        auto* message_label = new QLabel(message, &dialog);
        message_label->setWordWrap(true);
        layout->addWidget(message_label);
    }

    auto* form = new QFormLayout();
    std::vector<std::pair<std::string, QWidget*>> widgets;
    if (fields.is_array()) {
        for (const auto& field : fields) {
            if (!field.is_object()) continue;
            const std::string name = field.value("name", std::string{});
            if (name.empty()) continue;
            const QString label = QString::fromStdString(field.value("label", name));
            const QString default_value = field.contains("default")
                ? jsonToQString(field["default"])
                : QString{};
            QWidget* editor = nullptr;
            const auto choices_it = field.find("choices");
            if (choices_it != field.end() && choices_it->is_array()) {
                auto* combo = new QComboBox(&dialog);
                for (const auto& choice : *choices_it) {
                    combo->addItem(jsonToQString(choice));
                }
                const int index = combo->findText(default_value);
                if (index >= 0) combo->setCurrentIndex(index);
                editor = combo;
            } else {
                auto* line_edit = new QLineEdit(default_value, &dialog);
                editor = line_edit;
            }
            form->addRow(label, editor);
            widgets.emplace_back(name, editor);
        }
    }
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = ok;
    nlohmann::json result = nlohmann::json::object();
    if (!ok) return result;
    for (const auto& [name, widget] : widgets) {
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            result[name] = combo->currentText().toStdString();
        } else if (auto* line_edit = qobject_cast<QLineEdit*>(widget)) {
            result[name] = line_edit->text().toStdString();
        }
    }
    return result;
}

}  // namespace

MainWindow::MainWindow(std::string agents_config_path,
                       std::string nodes_root,
                       std::string echo_python_root,
                       std::string data_root,
                       std::string python_bin,
                       std::string node_runtime_module,
                       std::string data_registry_host,
                       int data_registry_port,
                       QWidget* parent)
    : QMainWindow(parent),
      agents_config_path_(std::move(agents_config_path)),
      nodes_root_(QString::fromStdString(nodes_root)),
      echo_python_root_(QString::fromStdString(echo_python_root)),
      data_root_(QString::fromStdString(data_root)),
      python_bin_(QString::fromStdString(python_bin)),
      node_runtime_module_(QString::fromStdString(node_runtime_module)),
      data_registry_host_(QString::fromStdString(data_registry_host)),
      data_registry_port_(data_registry_port) {
    ::setenv("RECORDLAB_DATA_REGISTRY_HOST", data_registry_host.c_str(), 1);
    ::setenv("RECORDLAB_DATA_REGISTRY_PORT", std::to_string(data_registry_port).c_str(), 1);
    if (!python_bin.empty()) {
        ::setenv("RECORDLAB_PYTHON_BIN", python_bin.c_str(), 1);
    }
    if (!node_runtime_module.empty()) {
        ::setenv("RECORDLAB_NODE_RUNTIME_MODULE", node_runtime_module.c_str(), 1);
    }
    setObjectName(QStringLiteral("recordlab_main_window"));
    setWindowTitle(QStringLiteral("RecordLab"));
    resize(1440, 900);

    // ── Architecture components (PLAN.md) ──────────────────────
    bus_.registerConsumer(msg::UI);

    agent_manager_ = std::make_unique<AgentManager>(
        bus_, agents_config_path_, nodes_root, echo_python_root, python_bin, node_runtime_module);
    agent_manager_->start();

    data_receiver_ = std::make_unique<DataReceiver>(bus_);
    data_registry_server_ = std::make_unique<DataRegistryServer>(
        bus_, data_registry_host, data_registry_port);
    data_registry_server_->start();

    watchdog_ = std::make_unique<Watchdog>(bus_);
    watchdog_->start();

    scripts_actuator_ = std::make_unique<ScriptsActuator>(
        bus_, nodes_root_, echo_python_root_,
        QString::fromStdString(agents_config_path_), python_bin_, this);
    const QString data_root_qt = data_root_.trimmed().isEmpty()
        ? QDir(nodes_root_).filePath(QStringLiteral("data"))
        : data_root_;

    // ── Bus poll timer (~60 Hz) ────────────────────────────────
    bus_poll_timer_ = new QTimer(this);
    bus_poll_timer_->setInterval(16);
    connect(bus_poll_timer_, &QTimer::timeout, this, [this]() {
        try {
            pollBusMessages();
        } catch (const std::exception& e) {
            reportQtException(QStringLiteral("pollBusMessages"), &e);
        } catch (...) {
            reportQtException(QStringLiteral("pollBusMessages"));
        }
    });
    bus_poll_timer_->start();

    // ── UI ─────────────────────────────────────────────────────
    stack_ = new QStackedWidget(this);
    entry_page_ = new EntryPage(stack_);
    workspace_page_ = new WorkspacePage(stack_);
    workspace_page_->bindMainWindow(this);
    workspace_page_->scriptPage()->setDataRoot(data_root_qt);
    workspace_page_->dataPage()->setDataRoot(data_root_qt);
    stack_->addWidget(entry_page_);
    stack_->addWidget(workspace_page_);
    setCentralWidget(stack_);

    connect(entry_page_, &EntryPage::agentSelected, this, [this](const QString& agent_name) {
        try {
            workspace_page_->activateAgent(agent_name);
            const auto config = agent_manager_->loadAgentConfig(agent_name.toStdString());
            QStringList scripts;
            for (const auto& script : config.default_scripts) {
                scripts << QString::fromStdString(script);
            }
            workspace_page_->scriptPage()->setScripts(scripts);
            workspace_page_->dataPage()->setCommands(config.exposed_commands);
            workspace_page_->configureSensorLayout(config.sensor_layout);
            stack_->setCurrentWidget(workspace_page_);
            showMaximized();
            this->activateAgent(agent_name);
        } catch (const std::exception& e) {
            reportQtException(QStringLiteral("agentSelected"), &e);
        } catch (...) {
            reportQtException(QStringLiteral("agentSelected"));
        }
    });
    connect(workspace_page_, &WorkspacePage::backRequested, this, [this]() {
        try {
            stack_->setCurrentWidget(entry_page_);
        } catch (const std::exception& e) {
            reportQtException(QStringLiteral("backRequested"), &e);
        } catch (...) {
            reportQtException(QStringLiteral("backRequested"));
        }
    });

    loadAgents();
}

MainWindow::~MainWindow() {
    shutdown();
}

EntryPage* MainWindow::entryPage() const {
    return entry_page_;
}

WorkspacePage* MainWindow::workspacePage() const {
    return workspace_page_;
}

// ── Bus polling ────────────────────────────────────────────────

void MainWindow::pollBusMessages() {
    auto messages = bus_.drainFor(msg::UI);
    for (const auto& m : messages) {
        try {
            handleUIMessage(m);
        } catch (const std::exception& e) {
            reportQtException(QStringLiteral("handleUIMessage/%1").arg(QString::fromStdString(m.type)), &e);
        } catch (...) {
            reportQtException(QStringLiteral("handleUIMessage/%1").arg(QString::fromStdString(m.type)));
        }
    }
}

void MainWindow::handleUIMessage(const HostMessage& m) {
    // ── DataReceiver messages ──
    if (m.type == msg::TOPIC_DATA) {
        const auto topic = m.payload.value("topic_name", std::string{});
        auto value = m.payload["value"];
        if (m.payload.contains("stream_frequencies_hz") && value.is_object()) {
            value["_stream_frequencies_hz"] = m.payload["stream_frequencies_hz"];
        }
        const double freq = m.payload.value("frequency_hz", 0.0);
        const bool first = m.payload.value("first_message", false);
        const QString name = QString::fromStdString(topic);
        if (first) appendLog(QStringLiteral("收到 topic: %1").arg(name));
        if (workspace_page_) {
            workspace_page_->handleTopicData(name, value, freq);
        }
        applyUiBindings(topic, value);
        return;
    }
    if (m.type == msg::DATA_REGISTERED) {
        if (data_receiver_) {
            data_receiver_->registerDataStream(dataStreamRegistrationFromJson(m.payload.value("stream", nlohmann::json::object())));
        }
        return;
    }
    if (m.type == msg::DATA_UNREGISTERED) {
        if (data_receiver_) {
            data_receiver_->unregisterDataStream(dataStreamRegistrationFromJson(m.payload.value("stream", nlohmann::json::object())));
        }
        return;
    }
    if (m.type == msg::NODE_COOKIES) {
        if (workspace_page_ && workspace_page_->dataPage()) {
            workspace_page_->dataPage()->setCookies(m.payload);
        }
        return;
    }

    // ── AgentManager messages ──
    if (m.type == msg::CMD_RESULT) {
        if (isCheckCommand(m.payload.value("cmd", std::string{}))) {
            return;
        }
        emit commandFinished(
            QString::fromStdString(m.payload.value("cmd", "")),
            m.payload.value("success", false),
            QString::fromStdString(m.payload.value("message", "")));
        return;
    }
    if (m.type == msg::WATCHDOG_STATE) {
        emit watchdogStateChanged(QString::fromStdString(m.payload.value("state", "")));
        return;
    }
    if (m.type == msg::USER_NOTIFICATION) {
        const auto rendered = renderNotification(m.payload);
        const QString title = rendered.first;
        const QString message = rendered.second;
        const QString severity = QString::fromStdString(m.payload.value("severity", std::string("info")));
        if (severity == QStringLiteral("critical") || severity == QStringLiteral("error")) {
            QMessageBox::critical(this, title, message);
        } else if (severity == QStringLiteral("warning")) {
            QMessageBox::warning(this, title, message);
        } else {
            QMessageBox::information(this, title, message);
        }
        return;
    }
    if (m.type == msg::UI_DIALOG_REQUEST) {
        handleDialogRequest(m);
        return;
    }
    if (m.type == msg::AGENT_ACTIVATED) {
        if (m.payload.value("success", false)) {
            const auto agent_name = m.payload.value("agent_name", std::string{});
            if (watchdog_) watchdog_->setActiveAgent(agent_name);
            if (data_receiver_) {
                const auto config = agent_manager_->loadAgentConfig(agent_name);
                for (const auto& t : config.topics) {
                    if (data_registry_server_) {
                        data_registry_server_->registerStatic({
                            t.name, "topic", config.subnode_host, config.data_port, config.name,
                            t.encoding, t.parse_mode, t.ui_max_hz, t.qos, t.metadata,
                        });
                    }
                }
            }
        }
        return;
    }

    // ── ScriptsActuator messages ──
    if (m.type == msg::SCRIPT_FINISHED) {
        emit scriptFinished(m.payload.value("exit_code", -1));
        return;
    }
    if (m.type == msg::SCRIPT_WORKFLOW) {
        const QString action = QString::fromStdString(m.payload.value("action", std::string{}));
        if (workspace_page_ && action == QStringLiteral("clear")) {
            workspace_page_->scriptPage()->clearWorkflow();
        } else if (workspace_page_) {
            workspace_page_->scriptPage()->updateWorkflow(
                QString::fromStdString(m.payload.value("title", std::string("脚本流程"))),
                QString::fromStdString(m.payload.value("message", std::string{})),
                QString::fromStdString(m.payload.value("steps_json", std::string("[]"))),
                jsonBoolValue(m.payload, "finished", false),
                jsonBoolValue(m.payload, "success", false));
        }
        return;
    }

    // ── Common ──
    if (m.type == msg::LOG_ENTRY) {
        const QString message = QString::fromStdString(m.payload.value("message", ""));
        if (!isCheckLogMessage(message)) {
            appendLog(message);
        }
        return;
    }
    if (m.type == msg::SCRIPT_OUTPUT) {
        appendLog(QString::fromStdString(m.payload.value("text", "")));
        return;
    }
}

// ── Logging ────────────────────────────────────────────────────

void MainWindow::appendLog(const QString& message) {
    if (message.trimmed().isEmpty()) return;
    const QString line = QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), message);
    common::Logger::instance().appendUiLine(line.toStdString());
    common::Logger::instance().log(common::LogLevel::Info, "UI", message.toStdString());
    emit logMessage(line);
}

void MainWindow::applyUiBindings(const std::string& topic, const nlohmann::json& value) {
    nlohmann::json bindings = nlohmann::json::object();
    if (!active_agent_.trimmed().isEmpty() && agent_manager_) {
        try {
            bindings = agent_manager_->loadAgentConfig(active_agent_.toStdString()).ui_bindings;
        } catch (...) {
        }
    }
    const auto topic_binding = bindings.is_object() ? bindings.find(topic) : bindings.end();
    if (topic_binding != bindings.end() && topic_binding->is_object()) {
        const std::string signal = topic_binding->value("signal", std::string{});
        const std::string field = topic_binding->value("field", std::string{});
        const double scale = topic_binding->value("scale", 1.0);
        if (!field.empty() && value.is_object()) {
            const auto it = value.find(field);
            if (it != value.end() && it->is_number()) {
                const double converted = it->get<double>() * scale;
                if (signal == "record_timer") emit recordTimerChanged(converted);
                if (signal == "time_delay") emit timeDelayChanged(converted);
                return;
            }
        }
    }
    if (topic == "record_timer" && value.is_object()) {
        emit recordTimerChanged(value.value("duration_ns", 0.0) / 1e9);
    } else if (topic == "time_delay" && value.is_object()) {
        emit timeDelayChanged(value.value("time_delay_ns", 0.0) / 1e6);
    }
}

QPair<QString, QString> MainWindow::renderNotification(const nlohmann::json& payload) const {
    QString title = QString::fromStdString(payload.value("title", std::string("RecordLab 提示")));
    QString message = QString::fromStdString(payload.value("message", std::string{}));
    const std::string code = payload.value("error_code", std::string{});
    if (!code.empty() && agent_manager_) {
        try {
            const auto agent_name = payload.value("agent_name", active_agent_.toStdString());
            const auto config = agent_manager_->loadAgentConfig(agent_name);
            const auto it = config.error_messages.find(code);
            if (it != config.error_messages.end() && it->is_object()) {
                title = QString::fromStdString(it->value("title", std::string("RecordLab 提示")));
                message = QString::fromStdString(it->value("message", std::string{}));
                nlohmann::json vars = payload.value("params", nlohmann::json::object());
                vars["agent_name"] = agent_name;
                vars["state"] = payload.value("state", std::string{});
                vars["reason"] = payload.value("reason", std::string{});
                title = replaceTemplateVars(title, vars);
                message = replaceTemplateVars(message, vars);
            }
        } catch (...) {
        }
    }
    return {title, message};
}

void MainWindow::handleDialogRequest(const HostMessage& m) {
    const QString dialog_id = QString::fromStdString(m.payload.value("dialog_id", m.payload.value("id", std::string{})));
    const QString kind = QString::fromStdString(m.payload.value("kind", std::string("info")));
    const QString title = QString::fromStdString(m.payload.value("title", std::string("RecordLab")));
    const QString message = QString::fromStdString(m.payload.value("message", std::string{}));
    nlohmann::json response = {
        {"dialog_id", dialog_id.toStdString()},
        {"id", dialog_id.toStdString()},
        {"success", true},
        {"cancelled", false},
    };
    if (kind == QStringLiteral("question")) {
        response["response"] = QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
    } else if (kind == QStringLiteral("input")) {
        bool ok = false;
        const QString value = QInputDialog::getText(this, title, message, QLineEdit::Normal,
                                                    QString::fromStdString(m.payload.value("default", std::string{})), &ok);
        response["cancelled"] = !ok;
        response["response"] = value.toStdString();
    } else if (kind == QStringLiteral("multi_field_input")) {
        bool ok = false;
        response["response"] = showMultiFieldInputDialog(this, title, message,
                                                         m.payload.value("fields", nlohmann::json::array()),
                                                         &ok);
        response["cancelled"] = !ok;
    } else {
        if (kind == QStringLiteral("error")) QMessageBox::critical(this, title, message);
        else if (kind == QStringLiteral("warning")) QMessageBox::warning(this, title, message);
        else QMessageBox::information(this, title, message);
        response["response"] = true;
    }
    bus_.publish({
        .source = msg::UI,
        .target = m.source.empty() ? msg::SCRIPTS_ACTUATOR : m.source,
        .type = msg::UI_DIALOG_RESPONSE,
        .payload = std::move(response),
    });
}

// ── Slots ──────────────────────────────────────────────────────

void MainWindow::activateAgent(const QString& agent_name) {
    try {
        if (watchdog_) watchdog_->clearActiveAgent();
        active_agent_ = agent_name;
        bus_.publish({
            .source = msg::UI, .target = msg::AGENT_MANAGER, .type = msg::ACTIVATE_AGENT,
            .payload = {{"agent_name", agent_name.toStdString()}},
        });
        // Watchdog is already running; it binds to this agent after AGENT_ACTIVATED success.
    } catch (const std::exception& e) {
        reportQtException(QStringLiteral("activateAgent"), &e);
    } catch (...) {
        reportQtException(QStringLiteral("activateAgent"));
    }
}

void MainWindow::sendCommand(const QString& cmd, const QString& params_json) {
    try {
        nlohmann::json params;
        const auto trimmed = params_json.trimmed();
        if (trimmed.isEmpty()) {
            params = nlohmann::json::object();
        } else {
            params = nlohmann::json::parse(trimmed.toStdString(), nullptr, false);
            if (params.is_discarded() || !params.is_object()) {
                emit commandFinished(cmd, false, QStringLiteral("命令参数必须是 JSON object"));
                return;
            }
        }
        const QString request_id = QStringLiteral("ui_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        bus_.publish({
            .request_id = request_id.toStdString(),
            .source = msg::UI, .target = msg::AGENT_MANAGER, .type = msg::CMD_REQUEST,
            .payload = {
                {"request_id", request_id.toStdString()},
                {"agent_name", active_agent_.toStdString()},
                {"cmd", cmd.toStdString()},
                {"params", params},
                {"priority", "normal"},
                {"silent", false},
            },
        });
    } catch (const std::exception& e) {
        reportQtException(QStringLiteral("sendCommand"), &e);
        emit commandFinished(cmd, false, QStringLiteral("命令发送失败: %1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        reportQtException(QStringLiteral("sendCommand"));
        emit commandFinished(cmd, false, QStringLiteral("命令发送失败: unknown exception"));
    }
}

void MainWindow::runScript(const QString& script_path) {
    try {
        bus_.publish({
            .source = msg::UI, .target = msg::SCRIPTS_ACTUATOR, .type = msg::RUN_SCRIPT,
            .payload = {{"script_path", script_path.trimmed().toStdString()}, {"agent_name", active_agent_.toStdString()}},
        });
    } catch (const std::exception& e) {
        reportQtException(QStringLiteral("runScript"), &e);
    } catch (...) {
        reportQtException(QStringLiteral("runScript"));
    }
}

void MainWindow::stopScript() {
    try {
        bus_.publish({
            .source = msg::UI, .target = msg::SCRIPTS_ACTUATOR, .type = msg::STOP_SCRIPT,
        });
    } catch (const std::exception& e) {
        reportQtException(QStringLiteral("stopScript"), &e);
    } catch (...) {
        reportQtException(QStringLiteral("stopScript"));
    }
}

void MainWindow::shutdown() {
    try {
        if (watchdog_) {
            watchdog_->stop();
            watchdog_.reset();
        }
        scripts_actuator_.reset();
        data_receiver_.reset();
        if (data_registry_server_) data_registry_server_->stop();
        data_registry_server_.reset();
        if (agent_manager_) agent_manager_->stop();
        agent_manager_.reset();
    } catch (const std::exception& e) {
        reportQtException(QStringLiteral("shutdown"), &e);
    } catch (...) {
        reportQtException(QStringLiteral("shutdown"));
    }
}

// ── Agent loading ─────────────────────────────────────────────

void MainWindow::loadAgents() {
    try {
        AgentConfigLoader loader(agents_config_path_);
        const auto agents = loader.loadPrimaryAgents();
        entry_page_->setAgents(agents);
        for (const auto& agent : agents) {
            workspace_page_->dataPage()->agentSelector()->addItem(QString::fromStdString(agent));
        }
    } catch (const std::exception& e) {
        entry_page_->setAgents({});
        entry_page_->summaryLabel()->setText(QStringLiteral("配置读取失败: %1").arg(QString::fromUtf8(e.what())));
        entry_page_->summaryLabel()->show();
    }
}

void MainWindow::reportQtException(const QString& context, const std::exception* error) {
    const QString detail = error ? QString::fromUtf8(error->what()) : QStringLiteral("unknown exception");
    const QString message = QStringLiteral("Qt 回调内部异常(%1): %2").arg(context, detail);
    try {
        appendLog(message);
    } catch (...) {
    }
    try {
        common::Logger::instance().log(common::LogLevel::Error, "UI", message.toStdString());
    } catch (...) {
    }
}

}  // namespace recordlab::host::ui
