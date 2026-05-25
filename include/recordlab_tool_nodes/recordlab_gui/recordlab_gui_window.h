#pragma once

#include "recordlab_core/master_client.h"
#include "recordlab_echo/echo.h"
#include "recordlab_tool_nodes/recordlab_gui/gui_config.h"

#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

class QLabel;
class QStackedWidget;
class QTabWidget;

namespace recordlab::nodes::gui {

class CommandPage;
class EntryPage;
class ScriptPage;

class RecordlabGuiWindow : public QWidget {
 public:
  explicit RecordlabGuiWindow(GuiConfig config, QWidget *parent = nullptr);

 private:
  void buildUi();
  void buildWorkspacePage();
  void selectMainAgent(const std::string &node);
  void runSelectedScripts(const QStringList &scripts);
  void stopScript();
  void executeCommand(std::string agent, std::string command, json params);
  void stopAllAgents();
  void connectRuntimeTopics();
  void subscribeOptional(const std::string &topic, std::function<void(const json &)> cb,
                         std::unique_ptr<Subscriber> &slot);
  void runCommandAsync(std::string agent, std::string command, json params);

  template <typename Fn>
  void invokeUi(Fn &&fn) {
    QMetaObject::invokeMethod(this, std::forward<Fn>(fn), Qt::QueuedConnection);
  }

  GuiConfig config_;
  MasterClient client_;
  QStackedWidget *stack_{nullptr};
  QLabel *main_agent_label_{nullptr};
  QLabel *timer_label_{nullptr};
  QLabel *delay_label_{nullptr};
  QLabel *watchdog_label_{nullptr};
  ScriptPage *script_page_{nullptr};
  CommandPage *command_page_{nullptr};
  std::string main_agent_;
  std::atomic<bool> script_running_{false};
  std::unique_ptr<Subscriber> log_sub_;
  std::unique_ptr<Subscriber> workflow_sub_;
  std::unique_ptr<Subscriber> watchdog_sub_;
  std::unique_ptr<Subscriber> timer_sub_;
  std::unique_ptr<Subscriber> delay_sub_;
};

}  // namespace recordlab::nodes::gui
