#pragma once

#include "recordlab_master/registries.h"
#include "recordlab_tool_nodes/recordlab_gui/gui_config.h"

#include <QWidget>

#include <functional>
#include <string>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;

namespace recordlab::nodes::gui {

class CommandPage : public QWidget {
 public:
  CommandPage(const GuiConfig &config,
              std::function<void(std::string, std::string, json)> on_execute,
              std::function<void()> on_stop_all, QWidget *parent = nullptr);

  void setCurrentAgent(const std::string &agent);
  void setWatchdogText(const QString &text);
  void appendLog(const QString &line);

 private:
  void executeCurrentCommand();
  void updateStatus();

  const GuiConfig &config_;
  std::function<void(std::string, std::string, json)> on_execute_;
  std::function<void()> on_stop_all_;
  QComboBox *agent_combo_{nullptr};
  QTextEdit *command_name_edit_{nullptr};
  QPlainTextEdit *command_params_edit_{nullptr};
  QPushButton *execute_button_{nullptr};
  QPushButton *stop_all_button_{nullptr};
  QLabel *status_label_{nullptr};
  QPlainTextEdit *log_view_{nullptr};
  QString current_agent_{QStringLiteral("未选择")};
  QString watchdog_text_{QStringLiteral("Watchdog: 无监控")};
};

}  // namespace recordlab::nodes::gui
