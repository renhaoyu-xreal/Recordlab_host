#include "recordlab_host/common/logger.h"
#include "recordlab_host/common/process_handle.h"
#include "recordlab_host/common/runtime_config.h"
#include "recordlab_host/ui/main_window.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ── Logger init ─────────────────────────────────────────────
    const auto runtime = recordlab::host::RuntimeConfigLoader::load(
        QCoreApplication::applicationFilePath().toStdString(),
        argc > 1 ? argv[1] : std::string{});

    const QString log_dir = [&]() {
        const QByteArray env = qgetenv("RECORDLAB_LOG_DIR");
        if (!env.isEmpty()) return QString::fromLocal8Bit(env);
        const QString run_folder = QStringLiteral("recordlab_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        return QDir(QString::fromStdString(runtime.logs_root)).filePath(run_folder);
    }();
    QDir().mkpath(log_dir);
    recordlab::host::common::Logger::instance().init(log_dir.toStdString(), "ui.log", "all.log");

    // ── Startup cleanup: kill orphaned node_runtime processes ─────
    recordlab::host::ProcessHandle::killByCmdlinePattern(runtime.node_runtime_module);

    // ── UI (HostMessageBus, AgentManager, DataReceiver live inside MainWindow) ─
    recordlab::host::ui::MainWindow window(
        runtime.agents_config_path,
        runtime.nodes_root,
        runtime.echo_python_root,
        runtime.data_root,
        runtime.python_bin,
        runtime.node_runtime_module,
        runtime.data_registry_host,
        runtime.data_registry_port,
        runtime.app_version,
        runtime.update_info);
    window.show();
    return app.exec();
}
