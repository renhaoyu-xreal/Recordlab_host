#include "recordlab_system_nodes/launcher/launcher_config.h"

#include <cassert>
#include <fstream>
#include <sstream>

int main() {
  std::ifstream in(std::string(RECORDLAB_MASTER_SOURCE_DIR) + "/config/recordlab_launcher.json");
  assert(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();
  assert(text.find("/home/hyren") == std::string::npos);
  auto cfg = recordlab::nodes::loadLauncherConfig(recordlab::nodes::defaultLauncherConfigPath());
  assert(cfg.nodes.count("/bsp_node") == 1);
  assert(!cfg.nodes["/bsp_node"].argv.empty());
  return 0;
}
