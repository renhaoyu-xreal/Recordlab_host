#include "recordlab_host/ui/script_page.h"

#include "recordlab_host/ui/data_output_directory_widget.h"
#include "recordlab_host/ui/sensor_workspace_widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

namespace recordlab::host::ui {

namespace {

struct WorkflowStatusMeta {
    QString text;
    QString background;
    QString foreground;
    QString border;
};

WorkflowStatusMeta workflowStatusMeta(const QString& status) {
    if (status == QStringLiteral("running")) {
        return {QStringLiteral("运行中"), QStringLiteral("#E3F2FD"), QStringLiteral("#0D47A1"), QStringLiteral("#2196F3")};
    }
    if (status == QStringLiteral("success")) {
        return {QStringLiteral("成功"), QStringLiteral("#E8F5E9"), QStringLiteral("#1B5E20"), QStringLiteral("#4CAF50")};
    }
    if (status == QStringLiteral("failed")) {
        return {QStringLiteral("失败"), QStringLiteral("#FFEBEE"), QStringLiteral("#B71C1C"), QStringLiteral("#F44336")};
    }
    if (status == QStringLiteral("stopping")) {
        return {QStringLiteral("停止中"), QStringLiteral("#FFF3E0"), QStringLiteral("#E65100"), QStringLiteral("#FB8C00")};
    }
    if (status == QStringLiteral("stopped")) {
        return {QStringLiteral("已停止"), QStringLiteral("#ECEFF1"), QStringLiteral("#37474F"), QStringLiteral("#78909C")};
    }
    return {QStringLiteral("等待"), QStringLiteral("#F5F5F5"), QStringLiteral("#555555"), QStringLiteral("#BDBDBD")};
}

QString workflowStatusText(bool finished, bool success) {
    if (!finished) return QStringLiteral("运行中");
    return success ? QStringLiteral("已完成") : QStringLiteral("已失败");
}

}  // namespace

ScriptPage::ScriptPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("script_page"));

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(6, 6, 6, 6);
    root_layout->setSpacing(8);

    auto* top_splitter = new QSplitter(Qt::Horizontal, this);
    top_splitter->setChildrenCollapsible(false);
    sensor_workspace_ = new SensorWorkspaceWidget(top_splitter);
    top_splitter->addWidget(sensor_workspace_);

    auto* right_pane = new QWidget(top_splitter);
    right_pane->setMinimumWidth(260);
    right_pane->setMaximumWidth(340);
    auto* right_layout = new QVBoxLayout(right_pane);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(8);

    auto* script_group = new QGroupBox(QStringLiteral("脚本列表"), right_pane);
    auto* script_layout = new QVBoxLayout(script_group);
    script_list_ = new QListWidget(script_group);
    script_list_->setObjectName(QStringLiteral("script_list"));
    script_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    script_list_->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #ffffe0; border: 1px solid #888; padding: 5px; }"
        "QListWidget::item:selected { background-color: #d7e7ff; color: #000000; }"));
    script_layout->addWidget(script_list_);
    right_layout->addWidget(script_group, 2);

    auto* import_button = new QPushButton(QStringLiteral("导入新脚本"), right_pane);
    import_button->setObjectName(QStringLiteral("import_script_button"));
    import_button->setMinimumHeight(40);
    import_button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #e8e4dc; border: 1px solid #9f9788; border-radius: 4px; font-weight: 600; }"
        "QPushButton:hover { background-color: #fffdf2; }"));
    right_layout->addWidget(import_button);

    auto* run_row = new QHBoxLayout();
    auto* run_button = new QPushButton(QStringLiteral("开始执行"), right_pane);
    run_button->setObjectName(QStringLiteral("run_script_button"));
    run_button->setMinimumHeight(40);
    run_button->setStyleSheet(QStringLiteral("QPushButton { background-color: #90ee90; border: 2px solid #006400; border-radius: 5px; font-weight: 600; }"));
    auto* stop_button = new QPushButton(QStringLiteral("停止执行"), right_pane);
    stop_button->setObjectName(QStringLiteral("stop_script_button"));
    stop_button->setMinimumHeight(40);
    stop_button->setStyleSheet(QStringLiteral("QPushButton { background-color: #ffb6c1; border: 2px solid #8b0000; border-radius: 5px; font-weight: 600; }"));
    run_row->addWidget(run_button, 1);
    run_row->addWidget(stop_button, 1);
    right_layout->addLayout(run_row);
    connect(run_button, &QPushButton::clicked, this, [this]() {
        const QStringList scripts = selectedScripts();
        emit runScriptRequested(scripts.isEmpty() ? QString{} : scripts.first());
    });
    connect(stop_button, &QPushButton::clicked, this, &ScriptPage::stopScriptRequested);
    connect(import_button, &QPushButton::clicked, this, [this]() {
        const QString script_path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("导入新脚本"),
            QString{},
            QStringLiteral("Python 脚本 (*.py);;所有文件 (*)"));
        if (script_path.trimmed().isEmpty()) {
            return;
        }
        const QFileInfo info(script_path);
        const QString normalized = info.exists() ? info.absoluteFilePath() : script_path;
        const auto matches = script_list_->findItems(normalized, Qt::MatchExactly);
        if (matches.isEmpty()) {
            script_list_->addItem(normalized);
            script_list_->setCurrentRow(script_list_->count() - 1);
        } else {
            script_list_->setCurrentItem(matches.first());
        }
    });

    right_layout->addStretch(1);

    top_splitter->addWidget(right_pane);
    top_splitter->setStretchFactor(0, 1);
    top_splitter->setStretchFactor(1, 0);
    top_splitter->setSizes({1180, 280});
    root_layout->addWidget(top_splitter, 4);

    auto* bottom_splitter = new QSplitter(Qt::Horizontal, this);
    bottom_splitter->setObjectName(QStringLiteral("script_bottom_splitter"));
    bottom_splitter->setChildrenCollapsible(false);

    auto* log_group = new QGroupBox(QStringLiteral("执行日志"), bottom_splitter);
    auto* log_layout = new QVBoxLayout(log_group);
    log_view_ = new QPlainTextEdit(log_group);
    log_view_->setObjectName(QStringLiteral("script_log_view"));
    log_view_->setReadOnly(true);
    log_view_->setMaximumBlockCount(2000);
    log_view_->setPlaceholderText(QStringLiteral("脚本启动、停止、运行日志会持续输出到这里。"));
    log_view_->setStyleSheet(QStringLiteral("QPlainTextEdit { background-color: #ffffe0; border: 1px solid #888; padding: 5px; }"));
    log_layout->addWidget(log_view_);
    bottom_splitter->addWidget(log_group);

    output_tabs_ = new QTabWidget(bottom_splitter);
    output_tabs_->setObjectName(QStringLiteral("script_output_tabs"));
    output_tabs_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid #b6b0a4; background: #f4f1ea; }"
        "QTabBar::tab { min-height: 20px; padding: 3px 10px; background: #e8e4dc; border: 1px solid #b6b0a4; }"
        "QTabBar::tab:selected { background: #fffdf2; font-weight: 600; }"));
    auto* data_output = new DataOutputDirectoryWidget(QStringLiteral("data"), output_tabs_);
    data_output->setObjectName(QStringLiteral("script_data_output_widget"));
    connect(data_output, &DataOutputDirectoryWidget::messageReady, this, [this](const QString& message) {
            if (log_view_) log_view_->appendPlainText(message);
    });
    const int data_tab_index = output_tabs_->addTab(data_output, data_output->titleText());
    connect(data_output, &DataOutputDirectoryWidget::titleChanged, this,
            [this, data_tab_index](const QString& title) {
                if (output_tabs_) output_tabs_->setTabText(data_tab_index, title);
            });
    output_tabs_->addTab(buildWorkflowPanel(), QStringLiteral("流程状态"));
    bottom_splitter->addWidget(output_tabs_);
    bottom_splitter->setStretchFactor(0, 2);
    bottom_splitter->setStretchFactor(1, 3);
    bottom_splitter->setSizes({480, 720});
    root_layout->addWidget(bottom_splitter, 1);
}

SensorWorkspaceWidget* ScriptPage::sensorWorkspace() const {
    return sensor_workspace_;
}

QListWidget* ScriptPage::scriptList() const {
    return script_list_;
}

QPlainTextEdit* ScriptPage::logView() const {
    return log_view_;
}

void ScriptPage::setScripts(const QStringList& scripts) {
    script_list_->clear();
    script_list_->addItems(scripts);
    if (script_list_->count() > 0) {
        script_list_->setCurrentRow(0);
    }
}

void ScriptPage::setDataRoot(const QString& data_root) {
    if (!output_tabs_) return;
    if (auto* widget = output_tabs_->findChild<DataOutputDirectoryWidget*>(QStringLiteral("script_data_output_widget"))) {
        widget->setRootPath(data_root);
    }
}

QStringList ScriptPage::selectedScripts() const {
    QStringList scripts;
    if (!script_list_) return scripts;
    const auto selected = script_list_->selectedItems();
    for (auto* item : selected) {
        if (item && !item->text().trimmed().isEmpty()) scripts << item->text().trimmed();
    }
    if (scripts.isEmpty() && script_list_->currentItem()) {
        scripts << script_list_->currentItem()->text().trimmed();
    }
    return scripts;
}

QWidget* ScriptPage::buildWorkflowPanel() {
    workflow_panel_ = new QWidget(this);
    workflow_panel_->setObjectName(QStringLiteral("script_workflow_panel"));
    auto* layout = new QVBoxLayout(workflow_panel_);
    layout->setSpacing(8);

    workflow_title_label_ = new QLabel(QStringLiteral("暂无脚本流程"), workflow_panel_);
    workflow_title_label_->setObjectName(QStringLiteral("workflow_title_label"));
    workflow_title_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    workflow_title_label_->setStyleSheet(QStringLiteral("QLabel { font-weight: 600; }"));
    layout->addWidget(workflow_title_label_);

    auto* steps_scroll = new QScrollArea(workflow_panel_);
    steps_scroll->setWidgetResizable(true);
    steps_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    steps_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    steps_scroll->setMinimumHeight(100);
    auto* steps_container = new QWidget(steps_scroll);
    workflow_steps_layout_ = new QHBoxLayout(steps_container);
    workflow_steps_layout_->setContentsMargins(4, 4, 4, 4);
    workflow_steps_layout_->setSpacing(6);
    steps_scroll->setWidget(steps_container);
    layout->addWidget(steps_scroll);

    workflow_message_label_ = new QLabel(QStringLiteral("说明: --"), workflow_panel_);
    workflow_message_label_->setObjectName(QStringLiteral("workflow_message_label"));
    workflow_message_label_->setWordWrap(true);
    workflow_message_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    workflow_message_label_->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #FFFDF2; border: 1px solid #C8B36A; padding: 6px; }"));
    layout->addWidget(workflow_message_label_);
    layout->addStretch(1);
    return workflow_panel_;
}

void ScriptPage::clearWorkflow() {
    if (workflow_title_label_) workflow_title_label_->setText(QStringLiteral("暂无脚本流程"));
    if (workflow_message_label_) workflow_message_label_->setText(QStringLiteral("说明: --"));
    if (!workflow_steps_layout_) return;
    while (workflow_steps_layout_->count() > 0) {
        auto* item = workflow_steps_layout_->takeAt(0);
        if (auto* widget = item->widget()) widget->deleteLater();
        delete item;
    }
}

void ScriptPage::updateWorkflow(const QString& title, const QString& message,
                                const QString& steps_json, bool finished, bool success) {
    if (output_tabs_ && workflow_panel_) {
        const int index = output_tabs_->indexOf(workflow_panel_);
        if (index >= 0) output_tabs_->setCurrentIndex(index);
    }
    if (workflow_title_label_) {
        workflow_title_label_->setText(QStringLiteral("%1 [%2]").arg(
            title.isEmpty() ? QStringLiteral("脚本流程") : title,
            workflowStatusText(finished, success)));
    }
    if (!workflow_steps_layout_) return;
    while (workflow_steps_layout_->count() > 0) {
        auto* item = workflow_steps_layout_->takeAt(0);
        if (auto* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    QString focus_label;
    QString focus_message = message;
    try {
        const auto steps = nlohmann::json::parse(steps_json.toStdString());
        if (steps.is_array()) {
            nlohmann::json focus_step;
            const auto focus_statuses = {std::string("failed"), std::string("stopping"),
                                         std::string("running"), std::string("stopped")};
            for (const auto& target : focus_statuses) {
                for (const auto& step : steps) {
                    if (step.value("status", std::string()) == target) {
                        focus_step = step;
                        break;
                    }
                }
                if (!focus_step.is_null()) break;
            }
            if (focus_step.is_null()) {
                for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
                    if (it->value("status", std::string()) == "success") {
                        focus_step = *it;
                        break;
                    }
                }
            }
            for (qsizetype index = 0; index < static_cast<qsizetype>(steps.size()); ++index) {
                const auto& step = steps.at(static_cast<size_t>(index));
                const auto label = QString::fromStdString(step.value("label", std::string("步骤")));
                const auto status = QString::fromStdString(step.value("status", std::string("pending")));
                const auto meta = workflowStatusMeta(status);
                auto* step_label = new QLabel(QStringLiteral("%1\n[%2]").arg(label, meta.text), workflow_panel_);
                step_label->setAlignment(Qt::AlignCenter);
                step_label->setMinimumWidth(110);
                step_label->setStyleSheet(QStringLiteral(
                    "QLabel { background-color: %1; color: %2; border: 2px solid %3; border-radius: 6px; padding: 8px 10px; }")
                    .arg(meta.background, meta.foreground, meta.border));
                workflow_steps_layout_->addWidget(step_label);
                if (index < static_cast<qsizetype>(steps.size()) - 1) {
                    auto* arrow = new QLabel(QStringLiteral("->"), workflow_panel_);
                    arrow->setAlignment(Qt::AlignCenter);
                    arrow->setStyleSheet(QStringLiteral("QLabel { color: #888; font-weight: bold; }"));
                    workflow_steps_layout_->addWidget(arrow);
                }
            }
            if (!focus_step.is_null()) {
                focus_label = QString::fromStdString(focus_step.value("label", std::string("步骤")));
                const auto step_message = QString::fromStdString(focus_step.value("message", std::string()));
                if (!step_message.isEmpty()) focus_message = step_message;
            }
        }
    } catch (...) {
        auto* error_label = new QLabel(QStringLiteral("流程事件解析失败"), workflow_panel_);
        error_label->setStyleSheet(QStringLiteral(
            "QLabel { background-color: #FFEBEE; color: #B71C1C; border: 2px solid #F44336; border-radius: 6px; padding: 8px 10px; }"));
        workflow_steps_layout_->addWidget(error_label);
        focus_message = QStringLiteral("流程事件解析失败");
    }
    workflow_steps_layout_->addStretch();
    if (workflow_message_label_) {
        workflow_message_label_->setText(
            focus_label.isEmpty()
                ? QStringLiteral("说明: %1").arg(focus_message.isEmpty() ? QStringLiteral("--") : focus_message)
                : QStringLiteral("说明: %1 - %2").arg(focus_label, focus_message.isEmpty() ? QStringLiteral("--") : focus_message));
    }
}

}  // namespace recordlab::host::ui
