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
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUuid>
#include <QStackedWidget>

#include <exception>

namespace recordlab::host::ui {

namespace {

/// Default UI rate limits per topic category (Hz).
double uiRateForTopic(const std::string& name) {
    if (name == "imu_data")        return 30.0;
    if (name == "record_timer")    return 10.0;
    if (name == "time_delay")      return 10.0;
    if (name == "motion_status")   return 5.0;
    return 30.0;
}

}  // namespace

MainWindow::MainWindow(std::string agents_config_path,
                       std::string nodes_root,
                       std::string echo_python_root,
                       QWidget* parent)
    : QMainWindow(parent),
      agents_config_path_(std::move(agents_config_path)),
      nodes_root_(QString::fromStdString(nodes_root)),
      echo_python_root_(QString::fromStdString(echo_python_root)) {
    setObjectName(QStringLiteral("recordlab_main_window"));
    setWindowTitle(QStringLiteral("RecordLab"));
    resize(1440, 900);

    // ── Architecture components (PLAN.md) ──────────────────────
    bus_.registerConsumer(msg::UI);

    agent_manager_ = std::make_unique<AgentManager>(
        bus_, agents_config_path_, nodes_root, echo_python_root);
    agent_manager_->start();

    data_receiver_ = std::make_unique<DataReceiver>(bus_);

    scripts_actuator_ = std::make_unique<ScriptsActuator>(
        bus_, nodes_root_, echo_python_root_,
        QString::fromStdString(agents_config_path_), this);

    // ── Bus poll timer (~30 Hz) ────────────────────────────────
    bus_poll_timer_ = new QTimer(this);
    bus_poll_timer_->setInterval(33);
    connect(bus_poll_timer_, &QTimer::timeout, this, &MainWindow::pollBusMessages);
    bus_poll_timer_->start();

    // ── UI ─────────────────────────────────────────────────────
    stack_ = new QStackedWidget(this);
    entry_page_ = new EntryPage(stack_);
    workspace_page_ = new WorkspacePage(stack_);
    workspace_page_->bindMainWindow(this);
    stack_->addWidget(entry_page_);
    stack_->addWidget(workspace_page_);
    setCentralWidget(stack_);

    connect(entry_page_, &EntryPage::agentSelected, this, [this](const QString& agent_name) {
        workspace_page_->activateAgent(agent_name);
        QStringList scripts;
        for (const auto& script : agent_manager_->loadAgentConfig(agent_name.toStdString()).default_scripts) {
            scripts << QString::fromStdString(script);
        }
        workspace_page_->scriptPage()->setScripts(scripts);
        stack_->setCurrentWidget(workspace_page_);
        this->activateAgent(agent_name);
    });
    connect(workspace_page_, &WorkspacePage::backRequested, this, [this]() {
        stack_->setCurrentWidget(entry_page_);
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
    for (const auto& m : messages)
        handleUIMessage(m);
}

void MainWindow::handleUIMessage(const HostMessage& m) {
    // ── DataReceiver messages ──
    if (m.type == msg::TOPIC_DATA) {
        const auto topic = m.payload.value("topic_name", std::string{});
        const auto& value = m.payload["value"];
        const double freq = m.payload.value("frequency_hz", 0.0);
        const bool first = m.payload.value("first_message", false);
        const QString name = QString::fromStdString(topic);
        if (first) appendLog(QStringLiteral("收到 topic: %1").arg(name));
        emit topicDataReceived(name, QString::fromStdString(value.dump()), freq);
        if (name == QStringLiteral("record_timer") && value.is_object())
            emit recordTimerChanged(value.value("duration_ns", 0.0) / 1e9);
        else if (name == QStringLiteral("time_delay") && value.is_object())
            emit timeDelayChanged(value.value("time_delay_ns", 0.0) / 1e6);
        return;
    }

    // ── AgentManager messages ──
    if (m.type == msg::CMD_RESULT) {
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
    if (m.type == msg::AGENT_ACTIVATED) {
        if (m.payload.value("success", false) && data_receiver_) {
            const auto agent_name = m.payload.value("agent_name", std::string{});
            const auto config = agent_manager_->loadAgentConfig(agent_name);
            std::vector<DataReceiver::TopicConfig> topics;
            for (const auto& t : config.topics)
                topics.push_back({t.name, t.port, t.encoding, uiRateForTopic(t.name)});
            data_receiver_->subscribe(config.subnode_host, topics);
        }
        return;
    }

    // ── ScriptsActuator messages ──
    if (m.type == msg::SCRIPT_FINISHED) {
        emit scriptFinished(m.payload.value("exit_code", -1));
        return;
    }

    // ── Common ──
    if (m.type == msg::LOG_ENTRY) {
        appendLog(QString::fromStdString(m.payload.value("message", "")));
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

// ── Slots ──────────────────────────────────────────────────────

void MainWindow::activateAgent(const QString& agent_name) {
    // Stop old Watchdog if any.
    if (watchdog_) {
        watchdog_->stop();
        watchdog_.reset();
    }
    active_agent_ = agent_name;
    bus_.publish({
        .source = msg::UI, .target = msg::AGENT_MANAGER, .type = msg::ACTIVATE_AGENT,
        .payload = {{"agent_name", agent_name.toStdString()}},
    });
    // Start Watchdog in its own thread (PLAN.md T1).
    watchdog_ = std::make_unique<Watchdog>(bus_, agent_name.toStdString());
    watchdog_->start();
}

void MainWindow::sendCommand(const QString& cmd, const QString& params_json) {
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
}

void MainWindow::runScript(const QString& script_path) {
    bus_.publish({
        .source = msg::UI, .target = msg::SCRIPTS_ACTUATOR, .type = msg::RUN_SCRIPT,
        .payload = {{"script_path", script_path.toStdString()}, {"agent_name", active_agent_.toStdString()}},
    });
}

void MainWindow::stopScript() {
    bus_.publish({
        .source = msg::UI, .target = msg::SCRIPTS_ACTUATOR, .type = msg::STOP_SCRIPT,
    });
}

void MainWindow::shutdown() {
    if (watchdog_) {
        watchdog_->stop();
        watchdog_.reset();
    }
    scripts_actuator_.reset();
    data_receiver_.reset();
    if (agent_manager_) agent_manager_->stop();
    agent_manager_.reset();
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

}  // namespace recordlab::host::ui
