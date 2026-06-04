#include "recordlab_host/scripts/scripts_actuator.h"
#include "recordlab_host/bus/message_types.h"
#include "recordlab_host/common/logger.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
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
    const QString resolved = resolveScriptPath(QString::fromStdString(script_path));
    if (resolved.isEmpty()) {
        publishToUI(msg::LOG_ENTRY, {{"message", "没有选择脚本"}});
        return;
    }
    if (script_process_ && script_process_->state() != QProcess::NotRunning) {
        publishToUI(msg::LOG_ENTRY, {{"message", "已有脚本正在运行"}});
        return;
    }

    current_script_id_ = "script_" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    current_script_path_ = resolved.toStdString();
    current_agent_name_ = agent_name;
    current_script_pid_ = 0;
    script_process_ = std::make_unique<QProcess>();
    script_process_->setWorkingDirectory(nodes_root_);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"),
               nodes_root_ + QStringLiteral(":") + echo_python_root_ +
               QStringLiteral(":") + env.value(QStringLiteral("PYTHONPATH")));
    env.insert(QStringLiteral("RECORDLAB_AGENT"), QString::fromStdString(agent_name));
    env.insert(QStringLiteral("RECORDLAB_AGENTS_CONFIG"), agents_config_path_);
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    const std::string all_log_path = common::Logger::instance().allLogPath();
    if (!all_log_path.empty()) {
        env.insert(QStringLiteral("RECORDLAB_LOG_DIR"),
                   QFileInfo(QString::fromStdString(all_log_path)).dir().absolutePath());
    }
    script_process_->setProcessEnvironment(env);

    connect(script_process_.get(), &QProcess::readyReadStandardOutput, this, [this]() {
        const auto text = QString::fromUtf8(script_process_->readAllStandardOutput()).trimmed();
        if (!text.isEmpty()) {
            const auto pid = current_script_pid_ > 0
                ? current_script_pid_
                : static_cast<long long>(script_process_->processId());
            publishToUI(msg::SCRIPT_OUTPUT, {
                {"text", text.toStdString()},
                {"stream", "stdout"},
                {"process", "script"},
                {"script_path", current_script_path_},
                {"pid", pid},
                {"script_id", current_script_id_},
            });
            common::Logger::instance().log(common::LogLevel::Info, "ScriptsActuator", text.toStdString(), {
                {"stream", "stdout"},
                {"process", "script"},
                {"script_path", current_script_path_},
                {"pid", pid},
                {"script_id", current_script_id_},
            });
        }
    });
    connect(script_process_.get(), &QProcess::readyReadStandardError, this, [this]() {
        const auto text = QString::fromUtf8(script_process_->readAllStandardError()).trimmed();
        if (!text.isEmpty()) {
            const auto pid = current_script_pid_ > 0
                ? current_script_pid_
                : static_cast<long long>(script_process_->processId());
            publishToUI(msg::SCRIPT_OUTPUT, {
                {"text", text.toStdString()},
                {"stream", "stderr"},
                {"process", "script"},
                {"script_path", current_script_path_},
                {"pid", pid},
                {"script_id", current_script_id_},
            });
            common::Logger::instance().log(common::LogLevel::Warn, "ScriptsActuator", text.toStdString(), {
                {"stream", "stderr"},
                {"process", "script"},
                {"script_path", current_script_path_},
                {"pid", pid},
                {"script_id", current_script_id_},
            });
        }
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
            this, [this](int exit_code, QProcess::ExitStatus) {
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
    script_process_->start(python_bin, {resolved});
}

void ScriptsActuator::doStopScript() {
    if (script_process_ && script_process_->state() != QProcess::NotRunning) {
        script_process_->terminate();
        if (!script_process_->waitForFinished(2000)) {
            script_process_->kill();
        }
        publishToUI(msg::LOG_ENTRY, {{"message", "脚本已停止"}});
    }
}

QString ScriptsActuator::resolveScriptPath(const QString& script_path) const {
    if (script_path.trimmed().isEmpty()) return {};
    const QFileInfo direct(script_path);
    if (direct.exists()) return direct.absoluteFilePath();
    const QFileInfo in_scripts(QDir(nodes_root_).filePath(QStringLiteral("node_scripts/") + script_path));
    if (in_scripts.exists()) return in_scripts.absoluteFilePath();
    return script_path;
}

void ScriptsActuator::publishToUI(const std::string& type, nlohmann::json payload) {
    bus_.publish({.source = msg::SCRIPTS_ACTUATOR, .target = msg::UI, .type = type, .payload = std::move(payload)});
}

}  // namespace recordlab::host
