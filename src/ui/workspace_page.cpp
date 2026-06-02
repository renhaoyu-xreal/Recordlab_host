#include "recordlab_host/ui/workspace_page.h"

#include "recordlab_host/common/logger.h"
#include "recordlab_host/ui/data_page.h"
#include "recordlab_host/ui/main_window.h"
#include "recordlab_host/ui/sensor_workspace_widget.h"
#include "recordlab_host/ui/script_page.h"

#include <QFrame>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <chrono>

namespace recordlab::host::ui {

WorkspacePage::WorkspacePage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("workspace_page"));
    setStyleSheet(QStringLiteral(R"(
        WorkspacePage { background: #f4f1ea; }
        QTabWidget::pane { border: 1px solid #b6b0a4; background: #f4f1ea; }
        QTabBar::tab { min-width: 130px; padding: 8px 16px; background: #e8e4dc; border: 1px solid #b6b0a4; }
        QTabBar::tab:selected { background: #fffdf2; font-weight: 600; }
        QGroupBox { font-weight: 600; border: 1px solid #aaa; margin-top: 8px; padding-top: 8px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
    )"));

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(8);

    auto* toolbar_frame = new QFrame(this);
    toolbar_frame->setObjectName(QStringLiteral("workspace_toolbar"));
    toolbar_frame->setStyleSheet(QStringLiteral("QFrame { background: #f4f1ea; border: none; }"));
    auto* toolbar_layout = new QHBoxLayout(toolbar_frame);
    toolbar_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_layout->setSpacing(10);

    auto* back_button = new QPushButton(QStringLiteral("返回"), toolbar_frame);
    back_button->setObjectName(QStringLiteral("back_button"));
    back_button->setMinimumHeight(36);
    connect(back_button, &QPushButton::clicked, this, &WorkspacePage::backRequested);
    toolbar_layout->addWidget(back_button, 0);

    toolbar_layout->addStretch(1);
    timer_value_label_ = new QLabel(QStringLiteral("录制时长: --"), toolbar_frame);
    delay_value_label_ = new QLabel(QStringLiteral("时间延迟: --"), toolbar_frame);
    watchdog_value_label_ = new QLabel(QStringLiteral("Watchdog: 无监控"), toolbar_frame);
    toolbar_layout->addWidget(timer_value_label_);
    toolbar_layout->addWidget(delay_value_label_);
    toolbar_layout->addStretch(1);
    toolbar_layout->addWidget(watchdog_value_label_);
    root_layout->addWidget(toolbar_frame);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("main_tabs"));
    script_page_ = new ScriptPage(tabs_);
    data_page_ = new DataPage(tabs_);
    tabs_->addTab(script_page_, QStringLiteral("脚本执行"));
    tabs_->addTab(data_page_, QStringLiteral("数据 + 命令"));
    root_layout->addWidget(tabs_, 1);

    updateHeader();
}

void WorkspacePage::activateAgent(const QString& agent_name) {
    active_agent_ = agent_name;
    updateHeader();
    if (data_page_ && data_page_->agentSelector()->findText(agent_name) < 0) {
        data_page_->agentSelector()->addItem(agent_name);
    }
    if (data_page_) {
        data_page_->agentSelector()->setCurrentText(agent_name);
    }
}

QString WorkspacePage::activeAgent() const {
    return active_agent_;
}

QTabWidget* WorkspacePage::tabWidget() const {
    return tabs_;
}

ScriptPage* WorkspacePage::scriptPage() const {
    return script_page_;
}

DataPage* WorkspacePage::dataPage() const {
    return data_page_;
}

void WorkspacePage::bindMainWindow(MainWindow* mainWindow) {
    main_window_ = mainWindow;
    if (!main_window_) return;

    // Commands / scripts → MainWindow
    connect(data_page_, &DataPage::commandRequested, main_window_, &MainWindow::sendCommand);
    connect(data_page_, &DataPage::stopAllRequested, main_window_, &MainWindow::shutdown);
    connect(script_page_, &ScriptPage::runScriptRequested, main_window_, &MainWindow::runScript);
    connect(script_page_, &ScriptPage::stopScriptRequested, main_window_, &MainWindow::stopScript);

    // MainWindow signals → UI
    connect(main_window_, &MainWindow::logMessage, this, [this](const QString& message) {
        if (!message.trimmed().isEmpty()) {
            script_page_->logView()->appendPlainText(message);
            data_page_->logView()->appendPlainText(message);
        }
    });
    connect(main_window_, &MainWindow::watchdogStateChanged, this, [this](const QString& state) {
        watchdog_value_label_->setText(active_agent_.isEmpty()
            ? QStringLiteral("Watchdog: %1").arg(state)
            : QStringLiteral("Watchdog: 当前 Agent: %1 | %2").arg(active_agent_, state));
    });
    connect(main_window_, &MainWindow::topicDataReceived, this, [this](const QString& name, const QString& value_json, double frequency) {
        auto value = nlohmann::json::parse(value_json.toStdString(), nullptr, false);
        if (value.is_discarded()) {
            value = nlohmann::json::object();
        }
        if (name == QStringLiteral("camera_data")) {
            auto* active_workspace = tabs_->currentWidget() == script_page_
                ? script_page_->sensorWorkspace()
                : data_page_->sensorWorkspace();
            active_workspace->handleRealtimeData(name, value, frequency);
            static auto last_debug_log = std::chrono::steady_clock::time_point{};
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - last_debug_log).count() >= 1.0) {
                common::Logger::instance().log(
                    common::LogLevel::Debug,
                    "WorkspacePage",
                    "camera_data dispatched active_tab="
                        + tabs_->tabText(tabs_->currentIndex()).toStdString()
                        + " payload_json_bytes=" + std::to_string(value_json.size())
                        + " ui_frequency_hz=" + std::to_string(frequency));
                last_debug_log = now;
            }
        } else {
            script_page_->sensorWorkspace()->handleRealtimeData(name, value, frequency);
            data_page_->sensorWorkspace()->handleRealtimeData(name, value, frequency);
        }
        if (name == QStringLiteral("imu_data") && !saw_imu_data_) {
            saw_imu_data_ = true;
            script_page_->logView()->appendPlainText(QStringLiteral("UI 已接收 imu_data，实时值区域开始刷新。"));
            data_page_->logView()->appendPlainText(QStringLiteral("UI 已接收 imu_data，实时值区域开始刷新。"));
        }
    });
    connect(main_window_, &MainWindow::recordTimerChanged, this, [this](double seconds) {
        timer_value_label_->setText(QStringLiteral("录制时长: %1 s").arg(seconds, 0, 'f', 1));
    });
    connect(main_window_, &MainWindow::timeDelayChanged, this, [this](double milliseconds) {
        delay_value_label_->setText(QStringLiteral("时间延迟: %1 ms").arg(milliseconds, 0, 'f', 1));
    });
}

void WorkspacePage::updateHeader() {
    const QString suffix = active_agent_.trimmed().isEmpty()
        ? QStringLiteral("无监控")
        : QStringLiteral("当前 Agent: %1 | DISCONNECTED").arg(active_agent_);
    watchdog_value_label_->setText(QStringLiteral("Watchdog: %1").arg(suffix));
}

}  // namespace recordlab::host::ui
