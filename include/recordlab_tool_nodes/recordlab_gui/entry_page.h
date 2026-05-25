#pragma once

#include "recordlab_tool_nodes/recordlab_gui/gui_config.h"

#include <QWidget>

#include <functional>
#include <string>

class QGridLayout;

namespace recordlab::nodes::gui {

class EntryPage : public QWidget {
 public:
  EntryPage(const GuiConfig &config, std::function<void(std::string)> on_agent_selected,
            QWidget *parent = nullptr);

 private:
  void rebuildButtons(const GuiConfig &config);

  std::function<void(std::string)> on_agent_selected_;
  QGridLayout *button_grid_{nullptr};
};

}  // namespace recordlab::nodes::gui
