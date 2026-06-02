#include "recordlab_host/ui/imu_runtime_bridge.h"
#include "recordlab_host/bus/message_types.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

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

// ── Construction / destruction ─────────────────────────────────

ImuRuntimeBridge::ImuRuntimeBridge(std::string agents_config_path, QObject* parent)
    : QObject(parent), agents_config_path_(std::move(agents_config_path)) {
    // Resolve paths.
    const QFileInfo config_info(QString::fromStdString(agents_config_path_));
    nodes_root_ = config_info.dir().absolutePath();
    if (nodes_root_.endsWith(QStringLiteral("/config")))
        nodes_root_ = QFileInfo(nodes_root_ + QStringLiteral("/..")).absoluteFilePath();
    host_root_ = QFileInfo(QCoreApplication::applicationFilePath()).dir().absolutePath();
    if (host_root_.endsWith(QStringLiteral("/build")))
        host_root_ = QFileInfo(host_root_ + QStringLiteral("/..")).absoluteFilePath();
    const QByteArray nodes_root_env = qgetenv("RECORDLAB_NODES_ROOT");
    if (!nodes_root_env.isEmpty()) {
        nodes_root_ = QString::fromLocal8Bit(nodes_root_env);
    } else if (!QFileInfo(QDir(nodes_root_).filePath(QStringLiteral("recordlab_nodes/core/node_runtime.py"))).exists()) {
        nodes_root_ = QDir(host_root_).filePath(QStringLiteral("third_party/Recordlab_nodes"));
    }
    const QByteArray echo_root_env = qgetenv("ECHO_MESSAGE_SYSTEM_PYTHON_ROOT");
    echo_python_root_ = echo_root_env.isEmpty()
        ? QDir(host_root_).filePath(QStringLiteral("third_party/echo_message_system/python"))
        : QString::fromLocal8Bit(echo_root_env);

    // Logging.
    const QByteArray log_dir_env = qgetenv("RECORDLAB_LOG_DIR");
    if (!log_dir_env.isEmpty()) {
        log_dir_path_ = QString::fromLocal8Bit(log_dir_env);
    } else {
        const QString run_folder = QStringLiteral("recordlab_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        log_dir_path_ = QDir(host_root_).filePath(QStringLiteral("logs/") + run_folder);
    }
    QDir().mkpath(log_dir_path_);
    log_ui_path_ = QDir(log_dir_path_).filePath(QStringLiteral("ui.log"));
    log_all_path_ = QDir(log_dir_path_).filePath(QStringLiteral("all.log"));
    common::Logger::instance().init(log_dir_path_.toStdString(), "ui.log", "all.log");
    appendLog(QStringLiteral("日志目录: %1").arg(log_dir_path_));
    appendLog(QStringLiteral("UI 日志: %1").arg(log_ui_path_));
    appendLog(QStringLiteral("全量日志: %1").arg(log_all_path_));
    appendLog(QStringLiteral("Host app: %1").arg(QCoreApplication::applicationFilePath()));
    appendLog(QStringLiteral("Agents config: %1").arg(QString::fromStdString(agents_config_path_)));
    appendLog(QStringLiteral("Nodes root: %1").arg(nodes_root_));
    appendLog(QStringLiteral("Echo Python root: %1").arg(echo_python_root_));

    // Register UI as a bus consumer.
    bus_.registerConsumer(msg::UI);

    // Create architecture components.
    agent_manager_ = std::make_unique<AgentManager>(
        bus_, agents_config_path_, nodes_root_.toStdString(), echo_python_root_.toStdString());
    agent_manager_->start();

    scripts_actuator_ = std::make_unique<ScriptsActuator>(
        bus_, nodes_root_, echo_python_root_, QString::fromStdString(agents_config_path_), this);

    // Bus poll timer — bridges HostMessageBus into the Qt event loop.
    bus_poll_timer_ = new QTimer(this);
    bus_poll_timer_->setInterval(33);  // ~30 Hz
    connect(bus_poll_timer_, &QTimer::timeout, this, &ImuRuntimeBridge::pollBusMessages);
    bus_poll_timer_->start();
}

ImuRuntimeBridge::~ImuRuntimeBridge() {
    shutdown();
}

// ── Public queries ─────────────────────────────────────────────

std::vector<std::string> ImuRuntimeBridge::primaryAgents() const {
    return agent_manager_->primaryAgents();
}

std::vector<std::string> ImuRuntimeBridge::defaultScripts(const QString& agent_name) const {
    return agent_manager_->loadAgentConfig(agent_name.toStdString()).default_scripts;
}

QString ImuRuntimeBridge::nodesRoot() const {
    return nodes_root_;
}

QString ImuRuntimeBridge::nodeScriptsRoot() const {
    return QDir(nodes_root_).filePath(QStringLiteral("node_scripts"));
}

// ── Slots (all delegate to bus messages) ───────────────────────

void ImuRuntimeBridge::activateAgent(const QString& agent_name) {
    active_agent_ = agent_name;

    // Set up DataReceiver for this agent's topics.
    const auto config = agent_manager_->loadAgentConfig(agent_name.toStdString());
    setupDataReceiver(config);

    // Send activate request to AgentManager via bus.
    bus_.publish({
        .source = msg::UI, .target = msg::AGENT_MANAGER, .type = msg::ACTIVATE_AGENT,
        .payload = {{"agent_name", agent_name.toStdString()}},
    });

    // Block until agent activation completes (preserves existing synchronous API).
    // AgentManager will publish AGENT_ACTIVATED when done.
    for (int i = 0; i < 300; ++i) {  // up to 30 seconds
        auto opt = bus_.waitFor(msg::UI, 100);
        if (!opt) continue;
        handleUIMessage(*opt);
        if (opt->type == msg::AGENT_ACTIVATED) break;
    }
}

void ImuRuntimeBridge::sendCommand(const QString& cmd, const QString& params_json) {
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
    if (cmd == QStringLiteral("start_record") && !params.contains("dataset_name")) {
        params["dataset_name"] = QString("ui_record_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")))
            .toStdString();
    }
    bus_.publish({
        .source = msg::UI, .target = msg::AGENT_MANAGER, .type = msg::CMD_REQUEST,
        .payload = {{"cmd", cmd.toStdString()}, {"params", params}},
    });
}

void ImuRuntimeBridge::runScript(const QString& script_path) {
    bus_.publish({
        .source = msg::UI, .target = msg::SCRIPTS_ACTUATOR, .type = msg::RUN_SCRIPT,
        .payload = {{"script_path", script_path.toStdString()}, {"agent_name", active_agent_.toStdString()}},
    });
}

void ImuRuntimeBridge::stopScript() {
    bus_.publish({
        .source = msg::UI, .target = msg::SCRIPTS_ACTUATOR, .type = msg::STOP_SCRIPT,
    });
}

void ImuRuntimeBridge::shutdown() {
    scripts_actuator_.reset();
    data_receiver_.reset();
    if (agent_manager_) agent_manager_->stop();
    agent_manager_.reset();
}

// ── DataReceiver setup ─────────────────────────────────────────

void ImuRuntimeBridge::setupDataReceiver(const AgentConfig& config) {
    data_receiver_.reset();
    data_receiver_ = std::make_unique<DataReceiver>(bus_);
    std::vector<DataReceiver::TopicConfig> topics;
    for (const auto& t : config.topics)
        topics.push_back({t.name, t.port, t.encoding, uiRateForTopic(t.name)});
    data_receiver_->subscribe(config.subnode_host, topics);
}

// ── Bus polling ────────────────────────────────────────────────

void ImuRuntimeBridge::pollBusMessages() {
    auto messages = bus_.drainFor(msg::UI);
    for (const auto& m : messages)
        handleUIMessage(m);
}

void ImuRuntimeBridge::handleUIMessage(const HostMessage& m) {
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
        // Already handled by the blocking loop in activateAgent; just emit signals.
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

void ImuRuntimeBridge::appendLog(const QString& message) {
    if (message.trimmed().isEmpty()) return;
    const QString line = QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), message);
    common::Logger::instance().appendUiLine(line.toStdString());
    common::Logger::instance().log(common::LogLevel::Info, "UI", message.toStdString());
    emit logMessage(line);
}

}  // namespace recordlab::host::ui
