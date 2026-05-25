#include "recordlab_core/master_client.h"
#include "recordlab_master/master_server.h"
#include "recordlab_echo/echo.h"
#include "recordlab_system_nodes/launcher/launcher_node.h"

#include <cassert>
#include <chrono>
#include <thread>

int main() {
  recordlab::MasterServer server(5840, 5841, 1000);
  server.start();

  recordlab::nodes::LauncherConfig cfg;
  cfg.master_endpoint = "tcp://127.0.0.1:5840";
  cfg.nodes["/fake_node"] = recordlab::nodes::LauncherCommand{{"/bin/sleep", "1"}};
  recordlab::nodes::LauncherNode launcher(cfg);
  assert(launcher.start());

  recordlab::MasterClient client("tcp://127.0.0.1:5840");
  auto svc = client.lookupService("/launcher/start_node");
  assert(svc["ok"] == true);
  recordlab::ServiceClient start(svc["data"]["endpoint"], 1000);
  auto resp = start.call({{"node", "/fake_node"}});
  assert(resp["ok"] == true);
  assert(resp["data"]["status"] == "started");
  assert(resp["data"]["pid"].get<int>() > 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  launcher.stop();
  server.stop();
  return 0;
}
