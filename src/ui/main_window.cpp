#include "recordlab_host/ui/main_window.h"

#include "recordlab_host/agents/agent_config_loader.h"
#include "recordlab_host/ui/data_page.h"
#include "recordlab_host/ui/entry_page.h"
#include "recordlab_host/ui/imu_runtime_bridge.h"
#include "recordlab_host/ui/script_page.h"
#include "recordlab_host/ui/workspace_page.h"

#include <QStackedWidget>
#include <QComboBox>
#include <QLabel>

#include <exception>

namespace recordlab::host::ui {

MainWindow::MainWindow(std::string agents_config_path, QWidget* parent)
    : QMainWindow(parent), agents_config_path_(std::move(agents_config_path)) {
    setObjectName(QStringLiteral("recordlab_main_window"));
    setWindowTitle(QStringLiteral("RecordLab"));
    resize(1440, 900);

    stack_ = new QStackedWidget(this);
    runtime_ = new ImuRuntimeBridge(agents_config_path_, this);
    entry_page_ = new EntryPage(stack_);
    workspace_page_ = new WorkspacePage(stack_);
    workspace_page_->bindRuntime(runtime_);
    stack_->addWidget(entry_page_);
    stack_->addWidget(workspace_page_);
    setCentralWidget(stack_);

    connect(entry_page_, &EntryPage::agentSelected, this, [this](const QString& agent_name) {
        workspace_page_->activateAgent(agent_name);
        QStringList scripts;
        for (const auto& script : runtime_->defaultScripts(agent_name)) {
            scripts << QString::fromStdString(script);
        }
        workspace_page_->scriptPage()->setScripts(scripts);
        stack_->setCurrentWidget(workspace_page_);
        runtime_->activateAgent(agent_name);
    });
    connect(workspace_page_, &WorkspacePage::backRequested, this, [this]() {
        stack_->setCurrentWidget(entry_page_);
    });

    loadAgents();
}

EntryPage* MainWindow::entryPage() const {
    return entry_page_;
}

WorkspacePage* MainWindow::workspacePage() const {
    return workspace_page_;
}

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
