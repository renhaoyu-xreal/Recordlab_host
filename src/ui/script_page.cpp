#include "recordlab_host/ui/script_page.h"

#include "recordlab_host/ui/sensor_workspace_widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace recordlab::host::ui {

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
    right_pane->setMinimumWidth(280);
    right_pane->setMaximumWidth(380);
    auto* right_layout = new QVBoxLayout(right_pane);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(8);

    auto* script_group = new QGroupBox(QStringLiteral("脚本列表"), right_pane);
    auto* script_layout = new QVBoxLayout(script_group);
    script_list_ = new QListWidget(script_group);
    script_list_->setObjectName(QStringLiteral("script_list"));
    script_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    script_list_->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #ffffe0; border: 1px solid #888; padding: 5px; }"
        "QListWidget::item:selected { background-color: #d7e7ff; }"));
    script_list_->addItems({
        QStringLiteral("record_ur_gt_3dof.py"),
        QStringLiteral("check_environment.py"),
    });
    script_layout->addWidget(script_list_);
    right_layout->addWidget(script_group, 2);

    auto* select_row = new QHBoxLayout();
    auto* refresh_button = new QPushButton(QStringLiteral("刷新脚本"), right_pane);
    refresh_button->setObjectName(QStringLiteral("refresh_scripts_button"));
    refresh_button->setMinimumHeight(40);
    refresh_button->setStyleSheet(QStringLiteral("QPushButton { background-color: #90ee90; border: 2px solid #006400; border-radius: 5px; }"));
    auto* clear_button = new QPushButton(QStringLiteral("取消选择"), right_pane);
    clear_button->setObjectName(QStringLiteral("clear_scripts_button"));
    clear_button->setMinimumHeight(40);
    clear_button->setStyleSheet(QStringLiteral("QPushButton { background-color: #ffb6c1; border: 2px solid #8b0000; border-radius: 5px; }"));
    select_row->addWidget(refresh_button, 1);
    select_row->addWidget(clear_button, 1);
    right_layout->addLayout(select_row);

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
        auto* item = script_list_->currentItem();
        emit runScriptRequested(item ? item->text() : QString());
    });
    connect(stop_button, &QPushButton::clicked, this, &ScriptPage::stopScriptRequested);
    connect(clear_button, &QPushButton::clicked, script_list_, &QListWidget::clearSelection);

    right_layout->addStretch(1);

    top_splitter->addWidget(right_pane);
    top_splitter->setStretchFactor(0, 1);
    top_splitter->setStretchFactor(1, 0);
    top_splitter->setSizes({1100, 320});
    root_layout->addWidget(top_splitter, 4);

    auto* bottom_splitter = new QSplitter(Qt::Horizontal, this);
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

    auto* data_group = new QGroupBox(QStringLiteral("data 输出目录"), bottom_splitter);
    auto* data_layout = new QVBoxLayout(data_group);
    auto* data_label = new QLabel(QStringLiteral("third_party/Recordlab_nodes/data"), data_group);
    data_label->setObjectName(QStringLiteral("script_data_output_label"));
    data_label->setWordWrap(true);
    data_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    data_label->setStyleSheet(QStringLiteral("QLabel { background-color: #fffdf2; border: 1px solid #c8b36a; padding: 8px; }"));
    data_layout->addWidget(data_label);
    data_layout->addStretch(1);
    bottom_splitter->addWidget(data_group);
    bottom_splitter->setStretchFactor(0, 1);
    bottom_splitter->setStretchFactor(1, 1);
    bottom_splitter->setSizes({720, 520});
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

}  // namespace recordlab::host::ui
