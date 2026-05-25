#include "recordlab_core/logger.h"
#include "recordlab_core/node_base.h"
#include "recordlab_tool_nodes/recordlab_gui/gui_config.h"
#include "recordlab_tool_nodes/recordlab_gui/recordlab_gui_window.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char **argv) {
  recordlab::setLogComponent("recordlab_gui");
  QApplication app(argc, argv);
  std::string config_path = recordlab::nodes::defaultGuiConfigPath();
  if (argc >= 3 && std::string(argv[1]) == "--config") config_path = argv[2];

  try {
    auto config = recordlab::nodes::loadGuiConfig(config_path);
    recordlab::NodeBase gui_node("/recordlab_gui", "/tools", config.master_endpoint);
    if (!gui_node.start()) return 1;
    recordlab::nodes::gui::RecordlabGuiWindow gui(std::move(config));
    gui.show();
    return app.exec();
  } catch (const std::exception &e) {
    QMessageBox::critical(nullptr, QStringLiteral("Recordlab GUI"), QString::fromUtf8(e.what()));
    return 1;
  }
}
