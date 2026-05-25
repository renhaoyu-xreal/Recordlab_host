#include "recordlab_tool_nodes/recordlab_gui/command_page.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <stdexcept>

namespace recordlab::nodes::gui {

CommandPage::CommandPage(const GuiConfig &config,
                         std::function<void(std::string, std::string, json)> on_execute,
                         std::function<void()> on_stop_all, QWidget *parent)
    : QWidget(parent),
      config_(config),
      on_execute_(std::move(on_execute)),
      on_stop_all_(std::move(on_stop_all)) {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(10);

  auto *command_group = new QGroupBox(QStringLiteral("Agent 命令执行"), this);
  auto *layout = new QVBoxLayout(command_group);
  layout->setSpacing(10);

  auto *agent_row = new QHBoxLayout();
  agent_row->addWidget(new QLabel(QStringLiteral("选择 Agent:"), command_group));
  agent_combo_ = new QComboBox(command_group);
  for (const auto &agent : config_.primary_agents) agent_combo_->addItem(QString::fromStdString(agent.node));
  agent_row->addWidget(agent_combo_, 1);
  layout->addLayout(agent_row);

  auto *cmd_row = new QHBoxLayout();
  cmd_row->addWidget(new QLabel(QStringLiteral("命令名称:"), command_group));
  command_name_edit_ = new QTextEdit(command_group);
  command_name_edit_->setPlaceholderText(QStringLiteral("例如: check, connect, init, start, stop, release, close"));
  command_name_edit_->setMaximumHeight(44);
  cmd_row->addWidget(command_name_edit_, 1);
  layout->addLayout(cmd_row);

  layout->addWidget(new QLabel(QStringLiteral("命令参数 (JSON):"), command_group));
  command_params_edit_ = new QPlainTextEdit(command_group);
  command_params_edit_->setPlaceholderText(QStringLiteral("{\"param\": \"value\"}"));
  command_params_edit_->setMaximumHeight(130);
  command_params_edit_->setStyleSheet(QStringLiteral(
      "QPlainTextEdit { background-color: #FFFFE0; border: 1px solid #888; font-family: Courier; padding: 4px; }"));
  layout->addWidget(command_params_edit_);

  auto *button_row = new QHBoxLayout();
  execute_button_ = new QPushButton(QStringLiteral("执行命令"), command_group);
  stop_all_button_ = new QPushButton(QStringLiteral("停止所有 Agent"), command_group);
  execute_button_->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #90EE90; border: 2px solid #006400; font-weight: 600; }"));
  stop_all_button_->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #FFB6C1; border: 2px solid #8B0000; font-weight: 600; }"));
  button_row->addWidget(execute_button_);
  button_row->addWidget(stop_all_button_);
  layout->addLayout(button_row);

  auto *status_group = new QGroupBox(QStringLiteral("当前状态"), command_group);
  auto *status_layout = new QVBoxLayout(status_group);
  status_label_ = new QLabel(status_group);
  status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  status_label_->setStyleSheet(QStringLiteral(
      "QLabel { background-color: #fbfbfb; border: 1px solid #d5d5d5; padding: 8px; }"));
  status_layout->addWidget(status_label_);
  layout->addWidget(status_group);
  root->addWidget(command_group, 1);

  root->addWidget(new QLabel(QStringLiteral("运行日志:"), this));
  log_view_ = new QPlainTextEdit(this);
  log_view_->setReadOnly(true);
  log_view_->setMaximumBlockCount(2000);
  log_view_->setPlaceholderText(QStringLiteral("命令执行结果会持续回流到这里。"));
  log_view_->setStyleSheet(QStringLiteral(
      "QPlainTextEdit { background-color: #FFFFE0; border: 1px solid #888; padding: 10px; }"));
  root->addWidget(log_view_, 2);

  QObject::connect(execute_button_, &QPushButton::clicked, this, [this]() { executeCurrentCommand(); });
  QObject::connect(stop_all_button_, &QPushButton::clicked, this, [this]() {
    if (on_stop_all_) on_stop_all_();
  });
  QObject::connect(agent_combo_, &QComboBox::currentTextChanged, this, [this](const QString &text) {
    current_agent_ = text.trimmed().isEmpty() ? QStringLiteral("未选择") : text.trimmed();
    updateStatus();
  });
  updateStatus();
}

void CommandPage::setCurrentAgent(const std::string &agent) {
  current_agent_ = QString::fromStdString(agent.empty() ? std::string("未选择") : agent);
  if (agent_combo_ && !agent.empty()) agent_combo_->setCurrentText(QString::fromStdString(agent));
  updateStatus();
}

void CommandPage::setWatchdogText(const QString &text) {
  watchdog_text_ = text;
  updateStatus();
}

void CommandPage::appendLog(const QString &line) {
  if (log_view_) log_view_->appendPlainText(line);
}

void CommandPage::executeCurrentCommand() {
  const std::string agent = agent_combo_->currentText().trimmed().toStdString();
  const std::string command = command_name_edit_->toPlainText().trimmed().toStdString();
  if (agent.empty() || command.empty()) {
    appendLog(QStringLiteral("请输入 Agent 和命令名称。"));
    return;
  }

  json params = json::object();
  const std::string raw = command_params_edit_->toPlainText().trimmed().toStdString();
  if (!raw.empty()) {
    try {
      params = json::parse(raw);
      if (!params.is_object()) throw std::runtime_error("命令参数必须是 JSON 对象");
    } catch (const std::exception &e) {
      appendLog(QStringLiteral("命令参数格式错误: %1").arg(QString::fromUtf8(e.what())));
      return;
    }
  }

  appendLog(QStringLiteral("执行命令: %1 - %2 - %3")
                .arg(QString::fromStdString(agent), QString::fromStdString(command),
                     QString::fromStdString(params.dump())));
  if (on_execute_) on_execute_(agent, command, params);
}

void CommandPage::updateStatus() {
  if (!status_label_) return;
  status_label_->setText(QStringLiteral("当前 Agent: %1\n%2").arg(current_agent_, watchdog_text_));
}

}  // namespace recordlab::nodes::gui
