#include "recordlab_master/registries.h"
#include <cassert>
#include <thread>

int main() {
  recordlab::RegistryStore s(50);
  s.registerNode({{"node", "bsp_node"}, {"namespace", "/"}});
  assert(s.listNodes().size() == 1);
  s.registerPublisher({{"node", "/bsp_node"}, {"topic", "imu"}, {"namespace", "/bsp"}, {"msg_type", "ImuBatch"}, {"transport", {{"type", "shm_ring_buffer"}}}});
  assert(s.lookupTopic("/bsp/imu").size() == 1);
  s.registerService({{"node", "/bsp_node"}, {"service", "/bsp/check"}, {"endpoint", "tcp://127.0.0.1:1"}});
  assert(!s.lookupService("/bsp/check").is_null());
  s.registerAction({{"node", "/bsp_node"}, {"action", "/bsp/start"}});
  assert(s.lookupAction("/bsp/start")["endpoints"].contains("send_goal"));
  s.registerType({{"type_name", "recordlab_msgs/ImuBatch"}, {"version", "1"}});
  assert(s.lookupType("recordlab_msgs/ImuBatch")["version"] == "1");
  s.setParam({{"name", "/record/root"}, {"value", "/tmp"}});
  assert(s.getParam("/record/root") == "/tmp");
  std::this_thread::sleep_for(std::chrono::milliseconds(70));
  auto ev = s.checkLeases();
  assert(!ev.empty() && ev.front().type == "node_stale");
  return 0;
}
