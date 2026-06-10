#include "recordlab_host/ui/data_page.h"

#include "recordlab_host/ui/data_output_directory_widget.h"
#include "recordlab_host/ui/log_text_edit.h"
#include "recordlab_host/ui/sensor_workspace_widget.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QVBoxLayout>

namespace recordlab::host::ui {

DataPage::DataPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("data_command_page"));

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
    right_layout->setSpacing(10);

    auto* command_group = new QGroupBox(QStringLiteral("Agent 命令执行"), right_pane);
    auto* command_layout = new QVBoxLayout(command_group);

    auto* agent_row = new QHBoxLayout();
    agent_row->addWidget(new QLabel(QStringLiteral("选择 Agent:"), command_group));
    agent_selector_ = new QComboBox(command_group);
    agent_selector_->setObjectName(QStringLiteral("agent_selector"));
    agent_selector_->setEditable(false);
    agent_selector_->setStyleSheet(QStringLiteral("QComboBox { background-color: #ffffe0; padding: 5px; }"));
    agent_row->addWidget(agent_selector_, 1);
    command_layout->addLayout(agent_row);

    auto* command_row = new QHBoxLayout();
    command_row->addWidget(new QLabel(QStringLiteral("选择命令:"), command_group));
    command_combo_box_ = new QComboBox(command_group);
    command_combo_box_->setObjectName(QStringLiteral("command_combo_box"));
    command_combo_box_->setEditable(true);
    command_combo_box_->addItems({
        QStringLiteral("check"),
        QStringLiteral("init_device"),
        QStringLiteral("start_device"),
        QStringLiteral("stop_device"),
        QStringLiteral("start_record"),
        QStringLiteral("stop_record"),
        QStringLiteral("release_device"),
        QStringLiteral("estop"),
        QStringLiteral("control_device"),
    });
    command_combo_box_->setStyleSheet(QStringLiteral("QComboBox { background-color: #fffacd; border: 1px solid #888; padding: 5px; }"));
    command_row->addWidget(command_combo_box_, 1);
    command_layout->addLayout(command_row);

    command_layout->addWidget(new QLabel(QStringLiteral("命令参数 (JSON):"), command_group));
    command_params_edit_ = new QPlainTextEdit(command_group);
    command_params_edit_->setObjectName(QStringLiteral("command_params_edit"));
    command_params_edit_->setPlaceholderText(QStringLiteral("{\"param\": \"value\"}"));
    command_params_edit_->setMaximumHeight(120);
    command_params_edit_->setStyleSheet(QStringLiteral("QPlainTextEdit { background-color: #ffffe0; border: 1px solid #888; font-family: Courier; padding: 4px; }"));
    command_layout->addWidget(command_params_edit_);

    auto* execute_button = new QPushButton(QStringLiteral("执行命令"), command_group);
    execute_button->setObjectName(QStringLiteral("execute_command_button"));
    execute_button->setMinimumHeight(40);
    execute_button->setStyleSheet(QStringLiteral("QPushButton { background-color: #90ee90; border: 2px solid #006400; }"));
    command_layout->addWidget(execute_button);

    auto* stop_all_button = new QPushButton(QStringLiteral("停止所有 Agent"), command_group);
    stop_all_button->setObjectName(QStringLiteral("stop_all_agents_button"));
    stop_all_button->setMinimumHeight(40);
    stop_all_button->setStyleSheet(QStringLiteral("QPushButton { background-color: #ffb6c1; border: 2px solid #8b0000; }"));
    command_layout->addWidget(stop_all_button);
    connect(execute_button, &QPushButton::clicked, this, [this]() {
        emit commandRequested(command_combo_box_->currentText().trimmed(), command_params_edit_->toPlainText());
    });
    connect(stop_all_button, &QPushButton::clicked, this, &DataPage::stopAllRequested);

    right_layout->addWidget(command_group, 2);

    auto* cookie_group = new QGroupBox(QStringLiteral("节点上报信息"), right_pane);
    auto* cookie_layout = new QVBoxLayout(cookie_group);
    cookie_view_ = new QPlainTextEdit(cookie_group);
    cookie_view_->setObjectName(QStringLiteral("node_cookie_view"));
    cookie_view_->setReadOnly(true);
    cookie_view_->setMaximumBlockCount(200);
    cookie_view_->setPlainText(QStringLiteral("--"));
    cookie_view_->setStyleSheet(QStringLiteral("QPlainTextEdit { background-color: #f7fbff; border: 1px solid #8aa6c1; padding: 6px; }"));
    cookie_layout->addWidget(cookie_view_);
    right_layout->addWidget(cookie_group, 1);
    right_layout->addStretch(1);

    top_splitter->addWidget(right_pane);
    top_splitter->setStretchFactor(0, 1);
    top_splitter->setStretchFactor(1, 0);
    top_splitter->setSizes({1180, 280});
    root_layout->addWidget(top_splitter, 4);

    auto* bottom_splitter = new QSplitter(Qt::Horizontal, this);
    bottom_splitter->setObjectName(QStringLiteral("command_bottom_splitter"));
    bottom_splitter->setChildrenCollapsible(false);

    auto* log_group = new QGroupBox(QStringLiteral("运行日志"), bottom_splitter);
    auto* log_layout = new QVBoxLayout(log_group);
    log_view_ = new LogTextEdit(log_group);
    log_view_->setObjectName(QStringLiteral("command_log_view"));
    log_view_->setPlaceholderText(QStringLiteral("命令执行和状态日志会持续回流到这里。"));
    log_view_->setStyleSheet(QStringLiteral("QTextEdit { background-color: #ffffe0; border: 1px solid #888; padding: 10px; }"));
    log_layout->addWidget(log_view_);
    bottom_splitter->addWidget(log_group);

    auto* data_group = new QGroupBox(QStringLiteral("data输出目录"), bottom_splitter);
    data_group->setObjectName(QStringLiteral("command_data_output_group"));
    auto* data_layout = new QVBoxLayout(data_group);
    auto* data_output = new DataOutputDirectoryWidget(QStringLiteral("data"), data_group);
    data_output->setObjectName(QStringLiteral("command_data_output_widget"));
    data_group->setTitle(data_output->titleText());
    connect(data_output, &DataOutputDirectoryWidget::messageReady, this, [this](const QString& message) {
        if (log_view_) log_view_->appendLogEntry(
            message,
            LogTextEdit::inferLevel(message),
            QStringLiteral("data"));
    });
    connect(data_output, &DataOutputDirectoryWidget::titleChanged, data_group, &QGroupBox::setTitle);
    data_layout->addWidget(data_output);
    bottom_splitter->addWidget(data_group);
    bottom_splitter->setStretchFactor(0, 2);
    bottom_splitter->setStretchFactor(1, 3);
    bottom_splitter->setSizes({480, 720});
    root_layout->addWidget(bottom_splitter, 1);
}

SensorWorkspaceWidget* DataPage::sensorWorkspace() const {
    return sensor_workspace_;
}

QComboBox* DataPage::agentSelector() const {
    return agent_selector_;
}

QComboBox* DataPage::commandComboBox() const {
    return command_combo_box_;
}

QPlainTextEdit* DataPage::commandParamsEdit() const {
    return command_params_edit_;
}

LogTextEdit* DataPage::logView() const {
    return log_view_;
}

void DataPage::setDataRoot(const QString& data_root) {
    if (auto* widget = findChild<DataOutputDirectoryWidget*>(QStringLiteral("command_data_output_widget"))) {
        widget->setRootPath(data_root);
    }
}

void DataPage::setCommands(const std::vector<std::string>& commands) {
    if (!command_combo_box_ || commands.empty()) {
        return;
    }
    const QString current = command_combo_box_->currentText();
    command_combo_box_->clear();
    for (const auto& command : commands) {
        command_combo_box_->addItem(QString::fromStdString(command));
    }
    if (!current.trimmed().isEmpty()) {
        command_combo_box_->setCurrentText(current);
    }
}

void DataPage::setCookies(const nlohmann::json& cookies) {
    if (!cookie_view_) {
        return;
    }
    const nlohmann::json items = cookies.is_object()
        ? cookies.value("cookies", nlohmann::json::array())
        : cookies;
    QStringList lines;
    if (items.is_array()) {
        for (const auto& item : items) {
            if (!item.is_object()) {
                continue;
            }
            const bool display = item.value("is_display", item.value("isDisplay", false));
            if (!display) {
                continue;
            }
            const QString key = QString::fromStdString(item.value("key", std::string{}));
            if (key.trimmed().isEmpty()) {
                continue;
            }
            const auto value_it = item.find("value");
            QString value_text;
            if (value_it == item.end() || value_it->is_null()) {
                value_text = QStringLiteral("--");
            } else if (value_it->is_string()) {
                value_text = QString::fromStdString(value_it->get<std::string>());
            } else {
                value_text = QString::fromStdString(value_it->dump());
            }
            lines << QStringLiteral("%1: %2").arg(key, value_text);
        }
    }
    cookie_view_->setPlainText(lines.isEmpty() ? QStringLiteral("--") : lines.join(QStringLiteral("\n")));
}

}  // namespace recordlab::host::ui
