#include "recordlab_tool_nodes/recordlab_gui/script_page.h"

#include <QAbstractItemView>
#include <QDir>
#include <QDirIterator>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace recordlab::nodes::gui {

ScriptPage::ScriptPage(const GuiConfig &config, std::function<void(QStringList)> on_run,
                       std::function<void()> on_stop, QWidget *parent)
    : QWidget(parent), config_(config), on_run_(std::move(on_run)), on_stop_(std::move(on_stop)) {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(8);

  auto *script_group = new QGroupBox(QStringLiteral("脚本列表"), this);
  auto *script_layout = new QVBoxLayout(script_group);
  script_list_ = new QListWidget(script_group);
  script_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  script_list_->setMinimumHeight(240);
  script_list_->setStyleSheet(QStringLiteral(
      "QListWidget { background-color: #FFFFE0; border: 1px solid #888; padding: 5px; }"));
  script_layout->addWidget(script_list_);

  auto *button_row = new QHBoxLayout();
  refresh_scripts_button_ = new QPushButton(QStringLiteral("刷新脚本"), script_group);
  clear_scripts_button_ = new QPushButton(QStringLiteral("取消选择"), script_group);
  refresh_scripts_button_->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #90EE90; border: 2px solid #006400; border-radius: 5px; }"));
  clear_scripts_button_->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #FFB6C1; border: 2px solid #8B0000; border-radius: 5px; }"));
  button_row->addWidget(refresh_scripts_button_);
  button_row->addWidget(clear_scripts_button_);
  script_layout->addLayout(button_row);

  selected_scripts_label_ = new QLabel(QStringLiteral("已选: 0 个脚本"), script_group);
  selected_scripts_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  selected_scripts_label_->setStyleSheet(QStringLiteral(
      "QLabel { background-color: #FFFFE0; border: 1px solid #888; padding: 8px; }"));
  script_layout->addWidget(selected_scripts_label_);

  auto *execution_row = new QHBoxLayout();
  run_scripts_button_ = new QPushButton(QStringLiteral("开始执行"), script_group);
  stop_scripts_button_ = new QPushButton(QStringLiteral("停止执行"), script_group);
  run_scripts_button_->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #90EE90; border: 2px solid #006400; border-radius: 5px; font-weight: 600; }"));
  stop_scripts_button_->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #FFB6C1; border: 2px solid #8B0000; border-radius: 5px; font-weight: 600; }"));
  execution_row->addWidget(run_scripts_button_);
  execution_row->addWidget(stop_scripts_button_);
  script_layout->addLayout(execution_row);
  root->addWidget(script_group, 2);

  auto *bottom = new QSplitter(Qt::Horizontal, this);
  auto *log_group = new QGroupBox(QStringLiteral("执行日志"), bottom);
  auto *log_layout = new QVBoxLayout(log_group);
  log_view_ = new QPlainTextEdit(log_group);
  log_view_->setReadOnly(true);
  log_view_->setMaximumBlockCount(2000);
  log_view_->setPlaceholderText(QStringLiteral("脚本启动、停止、运行日志会持续输出到这里。"));
  log_view_->setStyleSheet(QStringLiteral(
      "QPlainTextEdit { background-color: #FFFFE0; border: 1px solid #888; padding: 5px; }"));
  log_layout->addWidget(log_view_);
  bottom->addWidget(log_group);

  auto *workflow_group = new QGroupBox(QStringLiteral("流程状态"), bottom);
  auto *workflow_layout = new QVBoxLayout(workflow_group);
  workflow_tree_ = new QTreeWidget(workflow_group);
  workflow_tree_->setHeaderLabels({QStringLiteral("步骤"), QStringLiteral("状态"), QStringLiteral("信息")});
  workflow_tree_->setStyleSheet(QStringLiteral(
      "QTreeWidget { background-color: #FFFDF2; border: 1px solid #C8B36A; padding: 5px; }"));
  workflow_layout->addWidget(workflow_tree_);
  bottom->addWidget(workflow_group);
  bottom->setSizes({650, 550});
  root->addWidget(bottom, 2);

  QObject::connect(refresh_scripts_button_, &QPushButton::clicked, this, [this]() { reloadScripts(); });
  QObject::connect(clear_scripts_button_, &QPushButton::clicked, this, [this]() {
    script_list_->clearSelection();
    refreshSelectedScripts();
  });
  QObject::connect(script_list_, &QListWidget::itemSelectionChanged, this,
                   [this]() { refreshSelectedScripts(); });
  QObject::connect(run_scripts_button_, &QPushButton::clicked, this, [this]() {
    if (on_run_) on_run_(selectedScriptPaths());
  });
  QObject::connect(stop_scripts_button_, &QPushButton::clicked, this, [this]() {
    if (on_stop_) on_stop_();
  });

  reloadScripts();
  refreshButtons();
}

QStringList ScriptPage::selectedScriptPaths() const {
  QStringList paths;
  for (auto *item : script_list_->selectedItems()) {
    const QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) paths << path;
  }
  return paths;
}

void ScriptPage::appendLog(const QString &line) {
  if (log_view_) log_view_->appendPlainText(line);
}

void ScriptPage::updateWorkflow(const json &message) {
  if (!workflow_tree_) return;
  workflow_tree_->clear();
  if (message.contains("title")) {
    workflow_tree_->setHeaderLabel(QString::fromStdString(message.value("title", "流程状态")));
  }
  for (const auto &step : message.value("steps", json::array())) {
    auto *item = new QTreeWidgetItem(workflow_tree_);
    item->setText(0, QString::fromStdString(step.value("label", step.value("key", ""))));
    item->setText(1, QString::fromStdString(step.value("status", "")));
    item->setText(2, QString::fromStdString(step.value("message", "")));
  }
  if (message.contains("message")) appendLog(QString::fromStdString(message.value("message", "")));
}

void ScriptPage::clearWorkflow() {
  if (workflow_tree_) workflow_tree_->clear();
}

void ScriptPage::setRunning(bool running) {
  running_ = running;
  refreshButtons();
}

void ScriptPage::reloadScripts() {
  script_list_->clear();
  for (const auto &root : config_.script_roots) {
    QDir dir(QString::fromStdString(root));
    if (!dir.exists()) continue;
    QDirIterator it(dir.absolutePath(), {QStringLiteral("*.py")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = QDir::cleanPath(it.next());
      auto *item = new QListWidgetItem(dir.relativeFilePath(path), script_list_);
      item->setData(Qt::UserRole, path);
      item->setToolTip(path);
    }
  }
  appendLog(QStringLiteral("已刷新脚本列表: %1 个").arg(script_list_->count()));
  refreshSelectedScripts();
}

void ScriptPage::refreshSelectedScripts() {
  selected_scripts_label_->setText(QStringLiteral("已选: %1 个脚本").arg(selectedScriptPaths().size()));
  refreshButtons();
}

void ScriptPage::refreshButtons() {
  const bool has_selection = !selectedScriptPaths().isEmpty();
  run_scripts_button_->setEnabled(!running_ && has_selection);
  stop_scripts_button_->setEnabled(running_);
  refresh_scripts_button_->setEnabled(!running_);
  clear_scripts_button_->setEnabled(!running_ && has_selection);
}

}  // namespace recordlab::nodes::gui
