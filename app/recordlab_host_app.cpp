#include "recordlab_host/agents/agent_manager.h"
#include "recordlab_host/common/process_handle.h"
#include "recordlab_host/data/data_receiver.h"
#include "recordlab_host/common/logger.h"
#include "recordlab_host/ui/main_window.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <filesystem>
#include <memory>

namespace {

std::string defaultAgentsConfig(char** argv) {
    namespace fs = std::filesystem;
    fs::path bin_path = fs::absolute(argv[0]).parent_path();
    fs::path host_root = bin_path.filename() == "build" ? bin_path.parent_path() : bin_path;
    return (host_root / "third_party" / "Recordlab_nodes" / "config" / "agents_config.json").string();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ── Logger init ─────────────────────────────────────────────
    const std::string config_path = argc > 1 ? argv[1] : defaultAgentsConfig(argv);
    const QString host_root = []() {
        QString root = QFileInfo(QCoreApplication::applicationFilePath()).dir().absolutePath();
        if (root.endsWith(QStringLiteral("/build")))
            root = QFileInfo(root + QStringLiteral("/..")).absoluteFilePath();
        return root;
    }();

    const QString log_dir = [&]() {
        const QByteArray env = qgetenv("RECORDLAB_LOG_DIR");
        if (!env.isEmpty()) return QString::fromLocal8Bit(env);
        const QString run_folder = QStringLiteral("recordlab_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        return QDir(host_root).filePath(QStringLiteral("logs/") + run_folder);
    }();
    QDir().mkpath(log_dir);
    recordlab::host::common::Logger::instance().init(log_dir.toStdString(), "ui.log", "all.log");

    // ── Path resolution ────────────────────────────────────────
    const QFileInfo config_info(QString::fromStdString(config_path));
    QString nodes_root = config_info.dir().absolutePath();
    if (nodes_root.endsWith(QStringLiteral("/config")))
        nodes_root = QFileInfo(nodes_root + QStringLiteral("/..")).absoluteFilePath();
    const QByteArray nodes_root_env = qgetenv("RECORDLAB_NODES_ROOT");
    if (!nodes_root_env.isEmpty()) {
        nodes_root = QString::fromLocal8Bit(nodes_root_env);
    } else if (!QFileInfo(QDir(nodes_root).filePath(QStringLiteral("recordlab_nodes/core/node_runtime.py"))).exists()) {
        nodes_root = QDir(host_root).filePath(QStringLiteral("third_party/Recordlab_nodes"));
    }

    const QByteArray echo_root_env = qgetenv("ECHO_MESSAGE_SYSTEM_PYTHON_ROOT");
    const QString echo_python_root = echo_root_env.isEmpty()
        ? QDir(host_root).filePath(QStringLiteral("third_party/echo_message_system/python"))
        : QString::fromLocal8Bit(echo_root_env);

    // ── Startup cleanup: kill orphaned node_runtime processes ─────
    recordlab::host::ProcessHandle::killByCmdlinePattern("recordlab_nodes.core.node_runtime");

    // ── UI (HostMessageBus, AgentManager, DataReceiver live inside MainWindow) ─
    recordlab::host::ui::MainWindow window(config_path, nodes_root.toStdString(),
                                            echo_python_root.toStdString());
    window.show();
    return app.exec();
}