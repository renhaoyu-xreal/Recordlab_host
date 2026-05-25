#include "recordlab_tool_nodes/recordlab_gui/recordlab_gui_window.h"

#include "recordlab_core/logger.h"
#include "recordlab_tool_nodes/recordlab_gui/command_page.h"
#include "recordlab_tool_nodes/recordlab_gui/entry_page.h"
#include "recordlab_tool_nodes/recordlab_gui/script_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace recordlab::nodes::gui {
namespace {

QString compactJson(const json &value) {
  return QString::fromStdString(value.dump());
}

std::string commandPrefixForNode(const std::string &node) {
  if (node == "/bsp_node") return "/bsp";
  if (node == "/mcu_node") return "/mcu";
  if (node == "/nviz_node") return "/nviz";
  std::string prefix = node;
  if (!prefix.empty() && prefix.front() != '/') prefix.insert(prefix.begin(), '/');
  const std::string suffix = "_node";
  if (prefix.size() > suffix.size() &&
      prefix.compare(prefix.size() - suffix.size(), suffix.size(), suffix) == 0) {
    prefix.erase(prefix.size() - suffix.size());
  }
  return prefix;
}

bool isLifecycleAction(const std::string &command) {
  static const std::set<std::string> actions = {"connect", "init", "start", "stop", "release", "close"};
  return actions.count(command) > 0;
}

}  // namespace

RecordlabGuiWindow::RecordlabGuiWindow(GuiConfig config, QWidget *parent)
    : QWidget(parent), config_(std::move(config)), client_(config_.master_endpoint) {
  setWindowTitle(QStringLiteral("RecordLabC 控制中心"));
  resize(1600, 980);
  setStyleSheet(QStringLiteral(R"(
    QWidget { background: #f4f1ea; color: #2f2a22; font-size: 13px; }
    QLabel[role="heroTitle"] { font-size: 30px; font-weight: 700; color: #1f2d26; }
    QLabel[role="heroSubtitle"] { color: #6a6258; font-size: 14px; }
    QGroupBox {
      background: #fbfaf7; border: 1px solid #d8cfbf; border-radius: 8px;
      margin-top: 12px; padding-top: 10px; font-weight: 600;
    }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #5e5446; }
    QPushButton {
      background: #efe7d8; border: 1px solid #b9ab8e; border-radius: 6px;
      padding: 8px 14px; min-height: 34px;
    }
    QPushButton:hover { background: #f5edde; }
    QPushButton:pressed { background: #e8dfce; }
    QPushButton:disabled { background: #d3d3d3; border-color: #888; color: #666; }
    QTabWidget::pane {
      border: 1px solid #d8cfbf; background: #fbfaf7; border-radius: 8px; top: -1px;
    }
    QTabBar::tab {
      background: #ece3d3; border: 1px solid #d8cfbf; border-bottom: none;
      border-top-left-radius: 6px; border-top-right-radius: 6px;
      padding: 8px 18px; margin-right: 3px; font-size: 10.5pt;
    }
    QTabBar::tab:selected { background: #fbfaf7; border-bottom: 2px solid #2f6b53; color: #1f1a15; }
    QPlainTextEdit, QTextEdit, QListWidget, QTreeWidget, QLineEdit, QComboBox {
      background: #fffdf8; border: 1px solid #d6ccb8; border-radius: 6px;
      selection-background-color: #d9e7ff;
    }
  )"));
  buildUi();
  connectRuntimeTopics();
}

void RecordlabGuiWindow::buildUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  stack_ = new QStackedWidget(this);
  root->addWidget(stack_);
  stack_->addWidget(new EntryPage(config_, [this](std::string node) { selectMainAgent(std::move(node)); }, this));
  buildWorkspacePage();
}

void RecordlabGuiWindow::buildWorkspacePage() {
  auto *page = new QWidget(this);
  auto *root = new QVBoxLayout(page);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(8);

  auto *toolbar = new QHBoxLayout();
  toolbar->setSpacing(10);
  auto *back = new QPushButton(QStringLiteral("返回"), page);
  QObject::connect(back, &QPushButton::clicked, this, [this]() { stack_->setCurrentIndex(0); });
  main_agent_label_ = new QLabel(QStringLiteral("当前主 Agent: 未选择"), page);
  timer_label_ = new QLabel(QStringLiteral("录制时长: --"), page);
  delay_label_ = new QLabel(QStringLiteral("时间延迟: --"), page);
  watchdog_label_ = new QLabel(QStringLiteral("Watchdog: 无监控"), page);
  toolbar->addWidget(back);
  toolbar->addWidget(main_agent_label_);
  toolbar->addStretch(1);
  toolbar->addWidget(timer_label_);
  toolbar->addWidget(delay_label_);
  toolbar->addStretch(1);
  toolbar->addWidget(watchdog_label_);
  root->addLayout(toolbar);

  auto *tabs = new QTabWidget(page);
  script_page_ = new ScriptPage(
      config_, [this](QStringList scripts) { runSelectedScripts(scripts); }, [this]() { stopScript(); }, tabs);
  command_page_ = new CommandPage(
      config_,
      [this](std::string agent, std::string command, json params) {
        executeCommand(std::move(agent), std::move(command), std::move(params));
      },
      [this]() { stopAllAgents(); }, tabs);
  tabs->addTab(script_page_, QStringLiteral("脚本执行"));
  tabs->addTab(command_page_, QStringLiteral("数据 + 命令"));
  root->addWidget(tabs, 1);
  stack_->addWidget(page);
}

void RecordlabGuiWindow::selectMainAgent(const std::string &node) {
  main_agent_ = node;
  main_agent_label_->setText(QStringLiteral("当前主 Agent: %1").arg(QString::fromStdString(node)));
  if (command_page_) command_page_->setCurrentAgent(node);
  try {
    auto lookup = client_.lookupService("/watchdog/set_target");
    if (!lookup.value("ok", false) || lookup["data"].is_null()) throw std::runtime_error("watchdog 未注册");
    ServiceClient service(lookup["data"].value("endpoint", ""), 1000);
    service.call({{"node", node}});
    if (script_page_) script_page_->appendLog(QStringLiteral("已选择主 Agent: %1").arg(QString::fromStdString(node)));
  } catch (const std::exception &e) {
    QMessageBox::warning(this, QStringLiteral("Watchdog"), QString::fromUtf8(e.what()));
  }
  stack_->setCurrentIndex(1);
}

void RecordlabGuiWindow::runSelectedScripts(const QStringList &scripts) {
  if (scripts.isEmpty()) {
    script_page_->appendLog(QStringLiteral("请先选择脚本。"));
    return;
  }
  if (script_running_.exchange(true)) {
    script_page_->appendLog(QStringLiteral("已有脚本在执行中。"));
    return;
  }
  script_page_->setRunning(true);
  const std::string agent = main_agent_;
  const std::string endpoint = config_.master_endpoint;
  std::vector<std::string> paths;
  for (const auto &path : scripts) paths.push_back(path.toStdString());

  std::thread([this, endpoint, agent, paths]() {
    for (const auto &script : paths) {
      if (!script_running_.load()) break;
      invokeUi([this, script]() {
        script_page_->clearWorkflow();
        script_page_->appendLog(QStringLiteral("启动脚本: %1").arg(QString::fromStdString(script)));
      });
      try {
        MasterClient client(endpoint);
        auto lookup = client.lookupAction("/script_runner/run_script");
        if (!lookup.value("ok", false) || lookup["data"].is_null()) throw std::runtime_error("script_runner 未注册");
        ActionClient action(lookup["data"]["endpoints"], 1000);
        auto goal_id = action.sendGoal({{"script_path", script}, {"args", json::array()}, {"main_agent", agent}});
        auto result = action.waitForResult(goal_id, 24 * 60 * 60 * 1000);
        invokeUi([this, result]() {
          script_page_->appendLog(QStringLiteral("脚本结束: %1").arg(compactJson(result)));
        });
      } catch (const std::exception &e) {
        invokeUi([this, msg = QString::fromUtf8(e.what())]() {
          script_page_->appendLog(QStringLiteral("脚本启动失败: %1").arg(msg));
        });
        break;
      }
    }
    script_running_ = false;
    invokeUi([this]() { script_page_->setRunning(false); });
  }).detach();
}

void RecordlabGuiWindow::stopScript() {
  try {
    auto lookup = client_.lookupService("/script_runner/stop_script");
    if (!lookup.value("ok", false) || lookup["data"].is_null()) throw std::runtime_error("script_runner stop service 未注册");
    ServiceClient service(lookup["data"].value("endpoint", ""), 1000);
    service.call({{"reason", "用户在 GUI 点击停止"}});
    script_running_ = false;
    script_page_->appendLog(QStringLiteral("已发送停止脚本请求"));
    script_page_->setRunning(false);
  } catch (const std::exception &e) {
    QMessageBox::critical(this, QStringLiteral("停止脚本"), QString::fromUtf8(e.what()));
  }
}

void RecordlabGuiWindow::executeCommand(std::string agent, std::string command, json params) {
  runCommandAsync(std::move(agent), std::move(command), std::move(params));
}

void RecordlabGuiWindow::stopAllAgents() {
  command_page_->appendLog(QStringLiteral("正在停止所有 Agent..."));
  for (const auto &agent : config_.primary_agents) {
    runCommandAsync(agent.node, "stop", json::object());
  }
}

void RecordlabGuiWindow::runCommandAsync(std::string agent, std::string command, json params) {
  const std::string endpoint = config_.master_endpoint;
  std::thread([this, endpoint, agent = std::move(agent), command = std::move(command), params = std::move(params)]() {
    try {
      MasterClient client(endpoint);
      const std::string prefix = commandPrefixForNode(agent);
      json result;
      if (command == "check") {
        auto lookup = client.lookupService(prefix + "/check");
        if (!lookup.value("ok", false) || lookup["data"].is_null()) throw std::runtime_error("service 未注册");
        ServiceClient service(lookup["data"].value("endpoint", ""), 1000);
        result = service.call(params);
      } else if (isLifecycleAction(command)) {
        auto lookup = client.lookupAction(prefix + "/" + command);
        if (!lookup.value("ok", false) || lookup["data"].is_null()) throw std::runtime_error("action 未注册");
        ActionClient action(lookup["data"]["endpoints"], 1000);
        auto goal_id = action.sendGoal(params);
        result = action.waitForResult(goal_id, 10 * 60 * 1000);
      } else {
        auto service_lookup = client.lookupService(prefix + "/" + command);
        if (service_lookup.value("ok", false) && !service_lookup["data"].is_null()) {
          ServiceClient service(service_lookup["data"].value("endpoint", ""), 1000);
          result = service.call(params);
        } else {
          auto action_lookup = client.lookupAction(prefix + "/" + command);
          if (!action_lookup.value("ok", false) || action_lookup["data"].is_null()) {
            throw std::runtime_error("未找到 service/action: " + prefix + "/" + command);
          }
          ActionClient action(action_lookup["data"]["endpoints"], 1000);
          auto goal_id = action.sendGoal(params);
          result = action.waitForResult(goal_id, 10 * 60 * 1000);
        }
      }
      invokeUi([this, agent, command, result]() {
        command_page_->appendLog(QStringLiteral("命令完成: %1 - %2 - %3")
                                     .arg(QString::fromStdString(agent), QString::fromStdString(command),
                                          compactJson(result)));
      });
    } catch (const std::exception &e) {
      invokeUi([this, agent, command, msg = QString::fromUtf8(e.what())]() {
        command_page_->appendLog(QStringLiteral("命令失败: %1 - %2 - %3")
                                     .arg(QString::fromStdString(agent), QString::fromStdString(command), msg));
      });
    }
  }).detach();
}

void RecordlabGuiWindow::connectRuntimeTopics() {
  subscribeOptional("/recordlab/user_log", [this](const json &msg) {
    script_page_->appendLog(QString::fromStdString("[" + msg.value("level", "INFO") + "] [" +
                                                   msg.value("source_node", "") + "] " +
                                                   msg.value("message", "")));
  }, log_sub_);
  subscribeOptional("/script_runner/workflow", [this](const json &msg) { script_page_->updateWorkflow(msg); },
                    workflow_sub_);
  subscribeOptional("/watchdog/state", [this](const json &msg) {
    const QString text = QStringLiteral("Watchdog: %1 | %2")
                             .arg(QString::fromStdString(msg.value("health", "")),
                                  QString::fromStdString(msg.value("message", "")));
    watchdog_label_->setText(text);
    command_page_->setWatchdogText(text);
  }, watchdog_sub_);
  subscribeOptional("/nviz/record_timer", [this](const json &msg) {
    timer_label_->setText(QStringLiteral("录制时长: %1 s").arg(msg.value("value", 0.0), 0, 'f', 1));
  }, timer_sub_);
  subscribeOptional("/nviz/time_delay", [this](const json &msg) {
    delay_label_->setText(QStringLiteral("时间延迟: %1 ms").arg(msg.value("value", 0.0), 0, 'f', 1));
  }, delay_sub_);
}

void RecordlabGuiWindow::subscribeOptional(const std::string &topic, std::function<void(const json &)> cb,
                                           std::unique_ptr<Subscriber> &slot) {
  try {
    auto lookup = client_.lookupTopic(topic);
    if (!lookup.value("ok", false) || lookup["data"].empty()) return;
    const std::string endpoint = lookup["data"][0]["transport"].value("endpoint", "");
    if (endpoint.empty()) return;
    slot = std::make_unique<Subscriber>(endpoint, topic, [this, cb = std::move(cb)](const json &msg) {
      invokeUi([cb, msg]() { cb(msg); });
    });
  } catch (const std::exception &e) {
    RL_LOG_WARN(std::string("GUI topic subscribe skipped: ") + topic + " " + e.what());
  }
}

}  // namespace recordlab::nodes::gui
