#include "recordlab_host/ui/main_window.h"

#include "recordlab_host/agents/agent_config_loader.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"
#include "recordlab_host/ui/data_page.h"
#include "recordlab_host/ui/entry_page.h"
#include "recordlab_host/ui/sensor_workspace_widget.h"
#include "recordlab_host/ui/script_page.h"
#include "recordlab_host/ui/workspace_page.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QUuid>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <exception>
#include <cstdlib>
#include <algorithm>
#include <unordered_set>

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

QString normalizedLogLevel(QString level) {
    level = level.trimmed();
    if (level.startsWith('[') && level.endsWith(']') && level.size() >= 2) {
        level = level.mid(1, level.size() - 2).trimmed();
    }
    level = level.toLower();
    if (level == QStringLiteral("success")
        || level == QStringLiteral("ok")
        || level == QStringLiteral("passed")
        || level == QStringLiteral("completed")) {
        return QStringLiteral("success");
    }
    if (level == QStringLiteral("warning")
        || level == QStringLiteral("warn")) {
        return QStringLiteral("warning");
    }
    if (level == QStringLiteral("error")
        || level == QStringLiteral("failed")
        || level == QStringLiteral("failure")) {
        return QStringLiteral("error");
    }
    if (level == QStringLiteral("success")
        || level == QStringLiteral("warning")
        || level == QStringLiteral("error")) {
        return level;
    }
    return QStringLiteral("info");
}

QString normalizedLogType(QString log_type) {
    log_type = log_type.trimmed();
    if (log_type.startsWith('[') && log_type.endsWith(']') && log_type.size() >= 2) {
        log_type = log_type.mid(1, log_type.size() - 2).trimmed();
    }
    log_type = log_type.toLower();
    return log_type.isEmpty() ? QStringLiteral("system") : log_type;
}

common::LogLevel toCommonLogLevel(const QString& level) {
    if (level == QStringLiteral("error")) return common::LogLevel::Error;
    if (level == QStringLiteral("warning")) return common::LogLevel::Warn;
    if (level == QStringLiteral("success")) return common::LogLevel::Info;
    return common::LogLevel::Info;
}

bool workflowStepHasStatus(const nlohmann::json& payload,
                           const std::string& step_key,
                           const std::string& status) {
    const auto steps_text = payload.value("steps_json", std::string("[]"));
    const auto steps = nlohmann::json::parse(steps_text, nullptr, false);
    if (!steps.is_array()) return false;
    for (const auto& step : steps) {
        if (step.value("key", std::string{}) == step_key &&
            step.value("status", std::string{}) == status) {
            return true;
        }
    }
    return false;
}

bool extractSummaryPollConfig(const nlohmann::json& sensor_layout,
                              QString& summary_data_name,
                              int& poll_interval_ms) {
    if (!sensor_layout.is_object()) {
        return false;
    }
    for (const auto& [topic, layout] : sensor_layout.items()) {
        if (!layout.is_object()) {
            continue;
        }
        if (layout.value("ui_widget", std::string{}) != "summary_value") {
            continue;
        }
        summary_data_name = QString::fromStdString(layout.value("display_name", topic));
        poll_interval_ms = std::max(200, layout.value("poll_interval_ms", 1000));
        return !summary_data_name.trimmed().isEmpty();
    }
    return false;
}

std::vector<std::string> filterScriptMonitoringAgents(
    const std::vector<std::string>& agent_names,
    AgentManager* agent_manager) {
    std::vector<std::string> filtered;
    std::unordered_set<std::string> seen;
    for (const auto& agent_name : agent_names) {
        if (agent_name.empty() || seen.count(agent_name) > 0) {
            continue;
        }
        bool monitorable = true;
        if (agent_manager) {
            try {
                const auto config = agent_manager->loadAgentConfig(agent_name);
                monitorable = config.process_type != "local_scripts";
            } catch (...) {
                monitorable = false;
            }
        }
        if (!monitorable) {
            continue;
        }
        seen.insert(agent_name);
        filtered.push_back(agent_name);
    }
    return filtered;
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

constexpr int kFormHistoryLimit = 5;

QString historyStoreKeyPart(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) value = QStringLiteral("default");
    return QString::fromLatin1(
        value.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QSettings makeFormHistorySettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("RecordLab"), QStringLiteral("RecordLabHost"));
}

QString dialogHistoryScope(const QString& kind,
                           const QString& title,
                           const QString& message,
                           const nlohmann::json& payload) {
    const auto custom_it = payload.find("history_key");
    if (custom_it != payload.end()) {
        const QString custom = jsonToQString(*custom_it).trimmed();
        if (!custom.isEmpty()) return custom;
    }
    return QStringLiteral("%1|%2|%3").arg(kind, title.trimmed(), message.trimmed());
}

QString fieldHistoryScope(const QString& dialog_scope,
                          const std::string& field_name,
                          const nlohmann::json& field = nlohmann::json::object()) {
    const auto custom_it = field.find("history_key");
    if (custom_it != field.end()) {
        const QString custom = jsonToQString(*custom_it).trimmed();
        if (!custom.isEmpty()) return custom;
    }
    return dialog_scope + QStringLiteral("|") + QString::fromStdString(field_name);
}

QStringList loadFormHistory(const QString& history_scope) {
    QSettings settings = makeFormHistorySettings();
    settings.beginGroup(QStringLiteral("form_history"));
    const QStringList values = settings.value(historyStoreKeyPart(history_scope)).toStringList();
    settings.endGroup();
    return values;
}

void saveFormHistory(const QString& history_scope, QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return;
    QStringList values = loadFormHistory(history_scope);
    values.removeAll(value);
    values.prepend(value);
    while (values.size() > kFormHistoryLimit) values.removeLast();
    QSettings settings = makeFormHistorySettings();
    settings.beginGroup(QStringLiteral("form_history"));
    settings.setValue(historyStoreKeyPart(history_scope), values);
    settings.endGroup();
}

QStringList mergeHistoryAndChoices(const QStringList& history_values,
                                   const QStringList& choice_values,
                                   const QString& fallback_value) {
    QStringList merged;
    auto append_unique = [&merged](const QString& value) {
        if (!value.isEmpty() && !merged.contains(value)) merged.append(value);
    };
    for (const auto& value : history_values) append_unique(value);
    append_unique(fallback_value);
    for (const auto& value : choice_values) append_unique(value);
    return merged;
}

QComboBox* createHistoryComboBox(QWidget* parent,
                                 const QString& history_scope,
                                 const QString& default_value,
                                 const QStringList& choices,
                                 bool has_choices) {
    auto* combo = new QComboBox(parent);
    combo->setEditable(!has_choices);
    combo->setInsertPolicy(QComboBox::NoInsert);
    const QStringList history_values = loadFormHistory(history_scope);
    const QString effective_default = !history_values.isEmpty() ? history_values.front() : default_value;
    combo->addItems(mergeHistoryAndChoices(history_values, choices, effective_default));
    if (combo->isEditable()) {
        combo->setCurrentText(effective_default);
        if (auto* editor = combo->lineEdit()) editor->setClearButtonEnabled(true);
    } else if (const int index = combo->findText(effective_default); index >= 0) {
        combo->setCurrentIndex(index);
    } else if (combo->count() > 0) {
        combo->setCurrentIndex(0);
    }
    return combo;
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
                                         const QString& dialog_history_scope,
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
    std::vector<std::pair<QString, QWidget*>> history_widgets;
    if (fields.is_array()) {
        for (const auto& field : fields) {
            if (!field.is_object()) continue;
            const std::string name = field.value("name", std::string{});
            if (name.empty()) continue;
            const QString label = QString::fromStdString(field.value("label", name));
            const QString default_value = field.contains("default")
                ? jsonToQString(field["default"])
                : QString{};
            const QString history_scope = fieldHistoryScope(dialog_history_scope, name, field);
            QWidget* editor = nullptr;
            const auto choices_it = field.find("choices");
            if (choices_it != field.end() && choices_it->is_array()) {
                QStringList choices;
                for (const auto& choice : *choices_it) {
                    choices.append(jsonToQString(choice));
                }
                auto* combo = createHistoryComboBox(&dialog, history_scope, default_value, choices, true);
                editor = combo;
            } else {
                editor = createHistoryComboBox(&dialog, history_scope, default_value, {}, false);
            }
            const int font_size_pt = field.value("font_size_pt", 0);
            if (font_size_pt > 0) {
                QFont editor_font = editor->font();
                editor_font.setPointSize(font_size_pt);
                editor->setFont(editor_font);
                editor->setMinimumHeight(font_size_pt * 2);
                if (auto* combo = qobject_cast<QComboBox*>(editor)) {
                    if (auto* line_edit = combo->lineEdit()) line_edit->setFont(editor_font);
                }
            }
            const int min_width = field.value("min_width", 0);
            if (min_width > 0) editor->setMinimumWidth(min_width);

            auto* label_widget = new QLabel(label, &dialog);
            if (font_size_pt > 0) {
                QFont label_font = label_widget->font();
                label_font.setPointSize(font_size_pt);
                label_widget->setFont(label_font);
            }
            form->addRow(label_widget, editor);
            widgets.emplace_back(name, editor);
            history_widgets.emplace_back(history_scope, editor);
        }
    }
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.adjustSize();
    const QSize dialog_size = dialog.sizeHint();
    dialog.resize(static_cast<int>(dialog_size.width() * 1.5), dialog_size.height());

    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = ok;
    nlohmann::json result = nlohmann::json::object();
    if (!ok) return result;
    for (const auto& [name, widget] : widgets) {
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            result[name] = combo->currentText().toStdString();
        }
    }
    for (const auto& [history_scope, widget] : history_widgets) {
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            saveFormHistory(history_scope, combo->currentText());
        }
    }
    return result;
}

QString showSingleFieldInputDialog(QWidget* parent,
                                   const QString& title,
                                   const QString& message,
                                   const QString& default_value,
                                   const QString& history_scope,
                                   bool* accepted) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    if (!message.trimmed().isEmpty()) {
        auto* message_label = new QLabel(message, &dialog);
        message_label->setWordWrap(true);
        layout->addWidget(message_label);
    }

    auto* combo = createHistoryComboBox(&dialog, history_scope, default_value, {}, false);
    combo->setMinimumWidth(360);
    layout->addWidget(combo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = ok;
    if (!ok) return {};
    const QString value = combo->currentText();
    saveFormHistory(history_scope, value);
    return value;
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
                       std::string app_version,
                       std::string update_info,
                       QWidget* parent)
    : QMainWindow(parent),
      agents_config_path_(std::move(agents_config_path)),
      nodes_root_(QString::fromStdString(nodes_root)),
      echo_python_root_(QString::fromStdString(echo_python_root)),
      data_root_(QString::fromStdString(data_root)),
      app_version_(QString::fromStdString(std::move(app_version)).trimmed()),
      update_info_(QString::fromStdString(std::move(update_info)).trimmed()),
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

    summary_poll_timer_ = new QTimer(this);
    summary_poll_timer_->setInterval(1000);
    connect(summary_poll_timer_, &QTimer::timeout, this, [this]() {
        try {
            pollAgentSummary();
        } catch (const std::exception& e) {
            reportQtException(QStringLiteral("pollAgentSummary"), &e);
        } catch (...) {
            reportQtException(QStringLiteral("pollAgentSummary"));
        }
    });

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
    initializeStatusBar();

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
    showStartupMessages();
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
        const QString name = QString::fromStdString(topic);
        if (workspace_page_) {
            if (m.payload.value("first_message", false)) {
                workspace_page_->scriptPage()->sensorWorkspace()->resetTopicData(name);
                workspace_page_->dataPage()->sensorWorkspace()->resetTopicData(name);
            }
            workspace_page_->handleTopicData(name, value, freq);
        }
        applyUiBindings(topic, value);
        return;
    }
    if (m.type == msg::DATA_REGISTERED) {
        const auto stream = m.payload.value("stream", nlohmann::json::object());
        const QString data_name = QString::fromStdString(stream.value("data_name", std::string{}));
        if (workspace_page_ && !data_name.isEmpty()) {
            workspace_page_->scriptPage()->sensorWorkspace()->resetTopicData(data_name);
            workspace_page_->dataPage()->sensorWorkspace()->resetTopicData(data_name);
        }
        if (data_receiver_) {
            data_receiver_->registerDataStream(dataStreamRegistrationFromJson(stream));
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
        if (handleSummaryCmdResult(m)) {
            return;
        }
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
            active_agent_ = QString::fromStdString(agent_name);
            active_agent_connected_ = true;
            if (watchdog_) {
                watchdog_->setActiveAgent(agent_name, m.payload.value("watchdog_start_device", true));
            }
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
            updateSummaryPollingForActiveAgent();
        } else {
            active_agent_connected_ = false;
            if (summary_poll_timer_) {
                summary_poll_timer_->stop();
            }
            summary_request_id_.clear();
            summary_data_name_.clear();
        }
        return;
    }

    // ── ScriptsActuator messages ──
    if (m.type == msg::SCRIPT_FINISHED) {
        pending_script_agents_.clear();
        script_monitoring_started_ = false;
        bus_.publish({
            .source = msg::UI,
            .target = msg::AGENT_MANAGER,
            .type = msg::RELEASE_INACTIVE_AGENTS,
        });
        if (watchdog_) {
            if (!active_agent_.trimmed().isEmpty()) {
                const std::string active_agent = active_agent_.toStdString();
                bool watchdog_start_device = true;
                if (agent_manager_) {
                    try {
                        watchdog_start_device = agent_manager_->loadAgentConfig(active_agent).watchdog_start_device;
                    } catch (...) {
                    }
                }
                watchdog_->setActiveAgent(active_agent, watchdog_start_device);
            } else {
                watchdog_->clearActiveAgent();
            }
        }
        emit scriptFinished(m.payload.value("exit_code", -1));
        return;
    }
    if (m.type == msg::SCRIPT_REQUIRED_AGENTS) {
        pending_script_agents_.clear();
        script_monitoring_started_ = false;
        std::vector<std::string> requested_agents;
        if (m.payload.contains("agent_names") && m.payload["agent_names"].is_array()) {
            for (const auto& item : m.payload["agent_names"]) {
                if (item.is_string()) requested_agents.push_back(item.get<std::string>());
            }
        }
        pending_script_agents_ = filterScriptMonitoringAgents(requested_agents, agent_manager_.get());
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
            if (watchdog_ && !script_monitoring_started_ && !pending_script_agents_.empty() &&
                workflowStepHasStatus(m.payload, "nodes_check", "success")) {
                watchdog_->setMonitoredAgents(pending_script_agents_, true);
                script_monitoring_started_ = true;
            }
        }
        return;
    }

    // ── Common ──
    if (m.type == msg::LOG_ENTRY) {
        const QString message = QString::fromStdString(m.payload.value("message", ""));
        if (!isCheckLogMessage(message)) {
            appendLog(
                message,
                QString::fromStdString(m.payload.value("level", std::string("info"))),
                QString::fromStdString(m.payload.value("log_type", std::string("system"))));
        }
        return;
    }
    if (m.type == msg::SCRIPT_OUTPUT) {
        return;
    }
}

// ── Logging ────────────────────────────────────────────────────

void MainWindow::appendLog(const QString& message, const QString& level, const QString& log_type) {
    if (message.trimmed().isEmpty()) return;
    const QString normalized_level = normalizedLogLevel(level);
    const QString normalized_type = normalizedLogType(log_type);
    const QString line = QStringLiteral("[%1] [%2] [%3] %4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             normalized_level.toUpper(),
             normalized_type.toUpper(),
             message);
    common::Logger::instance().appendUiLine(line.toStdString());
    common::Logger::instance().log(
        toCommonLogLevel(normalized_level),
        "UI",
        message.toStdString(),
        {{"level", normalized_level.toStdString()}, {"log_type", normalized_type.toStdString()}});
    emit logMessage(message, normalized_level, normalized_type);
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
    const QString history_scope = dialogHistoryScope(kind, title, message, m.payload);
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
        const QString value = showSingleFieldInputDialog(
            this,
            title,
            message,
            QString::fromStdString(m.payload.value("default", std::string{})),
            history_scope,
            &ok);
        response["cancelled"] = !ok;
        response["response"] = value.toStdString();
    } else if (kind == QStringLiteral("multi_field_input")) {
        bool ok = false;
        response["response"] = showMultiFieldInputDialog(this, title, message,
                                                         m.payload.value("fields", nlohmann::json::array()),
                                                         history_scope,
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
        if (summary_poll_timer_) {
            summary_poll_timer_->stop();
        }
        summary_request_id_.clear();
        summary_data_name_.clear();
        active_agent_ = agent_name;
        updateActiveAgentStatus();
        active_agent_connected_ = false;
        if (watchdog_) {
            bool watchdog_start_device = true;
            if (agent_manager_) {
                try {
                    watchdog_start_device = agent_manager_->loadAgentConfig(agent_name.toStdString()).watchdog_start_device;
                } catch (...) {
                }
            }
            watchdog_->setActiveAgent(agent_name.toStdString(), watchdog_start_device);
        }
        bus_.publish({
            .source = msg::UI, .target = msg::AGENT_MANAGER, .type = msg::ACTIVATE_AGENT,
            .payload = {{"agent_name", agent_name.toStdString()}},
        });
    } catch (const std::exception& e) {
        reportQtException(QStringLiteral("activateAgent"), &e);
    } catch (...) {
        reportQtException(QStringLiteral("activateAgent"));
    }
}

void MainWindow::initializeStatusBar() {
    if (app_version_.isEmpty()) {
        app_version_ = QStringLiteral("v1.0.0");
    }

    version_label_ = new QLabel(QStringLiteral("版本：%1").arg(app_version_), this);
    version_label_->setObjectName(QStringLiteral("app_version_status_label"));
    version_label_->setStyleSheet(QStringLiteral(
        "QLabel#app_version_status_label { color: #5f5649; padding: 0 8px; font-weight: 600; }"));

    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar { background: #e8e0d0; border-top: 1px solid #d4c7af; }"));
    statusBar()->addPermanentWidget(version_label_);
    statusBar()->showMessage(QStringLiteral("主 Agent：未选择"));
}

void MainWindow::updateActiveAgentStatus() {
    statusBar()->showMessage(active_agent_.trimmed().isEmpty()
        ? QStringLiteral("主 Agent：未选择")
        : QStringLiteral("主 Agent：%1").arg(active_agent_));
}

void MainWindow::showStartupMessages() {
    if (update_info_.isEmpty()) {
        return;
    }
    QTimer::singleShot(0, this, [this]() {
        auto* dialog = new QMessageBox(
            QMessageBox::Information,
            QStringLiteral("更新信息"),
            QStringLiteral("%1\n\n%2").arg(app_version_, update_info_),
            QMessageBox::Ok,
            this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->open();
    });
}

void MainWindow::updateSummaryPollingForActiveAgent() {
    if (!summary_poll_timer_) {
        return;
    }
    summary_poll_timer_->stop();
    summary_request_id_.clear();
    summary_data_name_.clear();
    summary_poll_interval_ms_ = 1000;
    if (active_agent_.trimmed().isEmpty() || !agent_manager_) {
        return;
    }
    try {
        const auto config = agent_manager_->loadAgentConfig(active_agent_.toStdString());
        if (!extractSummaryPollConfig(config.sensor_layout, summary_data_name_, summary_poll_interval_ms_)) {
            return;
        }
        summary_poll_timer_->setInterval(summary_poll_interval_ms_);
        pollAgentSummary();
        summary_poll_timer_->start();
    } catch (...) {
    }
}

void MainWindow::pollAgentSummary() {
    if (active_agent_.trimmed().isEmpty() || summary_data_name_.trimmed().isEmpty()) {
        return;
    }
    const QString request_id = QStringLiteral("summary_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    summary_request_id_ = request_id;
    bus_.publish({
        .request_id = request_id.toStdString(),
        .source = msg::UI,
        .target = msg::AGENT_MANAGER,
        .type = msg::CMD_REQUEST,
        .payload = {
            {"request_id", request_id.toStdString()},
            {"agent_name", active_agent_.toStdString()},
            {"cmd", "get_runtime_state"},
            {"params", nlohmann::json::object()},
            {"priority", "normal"},
            {"silent", true},
        },
    });
}

bool MainWindow::handleSummaryCmdResult(const HostMessage& m) {
    const std::string request_id = m.request_id.empty()
        ? m.payload.value("request_id", std::string{})
        : m.request_id;
    if (summary_request_id_.trimmed().isEmpty()
        || QString::fromStdString(request_id) != summary_request_id_) {
        return false;
    }
    summary_request_id_.clear();
    if (!workspace_page_ || summary_data_name_.trimmed().isEmpty()) {
        return true;
    }
    if (!m.payload.value("success", false)) {
        return true;
    }
    nlohmann::json summary = nlohmann::json::object();
    if (m.payload.contains("result") && m.payload["result"].is_object()) {
        summary = m.payload["result"];
    }
    if (summary.empty()) {
        summary = m.payload;
    }
    if (!summary.contains("latest_update_time")) {
        summary["latest_update_time"] = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")).toStdString();
    }
    workspace_page_->scriptPage()->sensorWorkspace()->handleSummaryData(summary_data_name_, summary);
    workspace_page_->dataPage()->sensorWorkspace()->handleSummaryData(summary_data_name_, summary);
    return true;
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
        if (summary_poll_timer_) {
            summary_poll_timer_->stop();
        }
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
        appendLog(message, QStringLiteral("error"), QStringLiteral("ui"));
    } catch (...) {
    }
    try {
        common::Logger::instance().log(common::LogLevel::Error, "UI", message.toStdString());
    } catch (...) {
    }
}

}  // namespace recordlab::host::ui
