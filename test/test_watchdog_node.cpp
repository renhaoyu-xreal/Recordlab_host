#include "recordlab_core/master_client.h"
#include "recordlab_master/master_server.h"
#include "recordlab_echo/echo.h"
#include "recordlab_system_nodes/watchdog/watchdog_node.h"

#include <cassert>
#include <chrono>
#include <mutex>
#include <thread>

int main() {
  using recordlab::json;

  recordlab::MasterServer server(5820, 5821, 150);
  server.start();

  recordlab::nodes::WatchdogNode watchdog("tcp://127.0.0.1:5820");
  assert(watchdog.start());
  recordlab::MasterClient client("tcp://127.0.0.1:5820");

  auto set_lookup = client.lookupService("/watchdog/set_target");
  assert(set_lookup["ok"] == true);
  recordlab::ServiceClient set_target(set_lookup["data"]["endpoint"], 1000);
  auto set_resp = set_target.call({{"node", "/bsp_node"}});
  assert(set_resp["ok"] == true);
  assert(watchdog.target() == "/bsp_node");

  auto offline = watchdog.evaluateTarget(client.listNodes()["data"], "/bsp_node");
  assert(offline["health"] == "offline");

  client.registerNode({{"node", "/bsp_node"}, {"kind", "device_node"}});
  auto ok = watchdog.evaluateTarget(client.listNodes()["data"], "/bsp_node");
  assert(ok["health"] == "ok");
  auto device_error = watchdog.evaluateTarget(
      client.listNodes()["data"], "/bsp_node",
      {{"node", "/bsp_node"},
       {"lifecycle_state", "connected"},
       {"health", "error"},
       {"message", "BSP device disconnected"}});
  assert(device_error["health"] == "error");
  assert(device_error["device_state"]["message"] == "BSP device disconnected");

  recordlab::Publisher bsp_state_pub("/bsp/state");
  assert(client.registerPublisher({{"node", "/bsp_node"},
                                   {"topic", "/bsp/state"},
                                   {"msg_type", "recordlab_msgs/DeviceState"},
                                   {"transport", {{"type", "tcp_pubsub"},
                                                   {"endpoint", bsp_state_pub.endpoint()}}}})["ok"] == true);
  json last_watchdog_state = json::object();
  std::mutex last_mu;
  auto watchdog_topic = client.lookupTopic("/watchdog/state");
  assert(watchdog_topic["ok"] == true);
  recordlab::Subscriber watchdog_state_sub(
      watchdog_topic["data"][0]["transport"]["endpoint"],
      "/watchdog/state",
      [&](const json &msg) {
        std::lock_guard<std::mutex> lock(last_mu);
        last_watchdog_state = msg;
      });
  set_resp = set_target.call({{"node", "/bsp_node"}});
  assert(set_resp["ok"] == true);
  for (int i = 0; i < 30; ++i) {
    client.heartbeat("/bsp_node");
    bsp_state_pub.publish({{"node", "/bsp_node"},
                           {"lifecycle_state", "connected"},
                           {"health", "error"},
                           {"message", "BSP device disconnected"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> lock(last_mu);
    if (last_watchdog_state.value("health", "") == "error") break;
  }
  {
    std::lock_guard<std::mutex> lock(last_mu);
    assert(last_watchdog_state["health"] == "error");
    assert(last_watchdog_state["device_state"]["message"] == "BSP device disconnected");
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto stale = watchdog.evaluateTarget(client.listNodes()["data"], "/bsp_node");
  assert(stale["health"] == "stale");

  auto clear_lookup = client.lookupService("/watchdog/clear_target");
  assert(clear_lookup["ok"] == true);
  recordlab::ServiceClient clear_target(clear_lookup["data"]["endpoint"], 1000);
  auto clear_resp = clear_target.call({});
  assert(clear_resp["ok"] == true);
  assert(watchdog.target().empty());

  watchdog.stop();
  server.stop();
  return 0;
}
