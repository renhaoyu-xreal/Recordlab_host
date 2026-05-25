#pragma once

#include "recordlab_master/registries.h"
#include "recordlab_tool_nodes/recordlab_gui/gui_config.h"

#include <QWidget>

#include <atomic>
#include <functional>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

namespace recordlab::nodes::gui {

class ScriptPage : public QWidget {
 public:
  ScriptPage(const GuiConfig &config, std::function<void(QStringList)> on_run,
             std::function<void()> on_stop, QWidget *parent = nullptr);

  void appendLog(const QString &line);
  void updateWorkflow(const json &message);
  void clearWorkflow();
  void setRunning(bool running);
  QStringList selectedScriptPaths() const;

 private:
  void reloadScripts();
  void refreshSelectedScripts();
  void refreshButtons();

  const GuiConfig &config_;
  std::function<void(QStringList)> on_run_;
  std::function<void()> on_stop_;
  QListWidget *script_list_{nullptr};
  QLabel *selected_scripts_label_{nullptr};
  QPushButton *refresh_scripts_button_{nullptr};
  QPushButton *clear_scripts_button_{nullptr};
  QPushButton *run_scripts_button_{nullptr};
  QPushButton *stop_scripts_button_{nullptr};
  QPlainTextEdit *log_view_{nullptr};
  QTreeWidget *workflow_tree_{nullptr};
  bool running_{false};
};

}  // namespace recordlab::nodes::gui
