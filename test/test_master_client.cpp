#include "recordlab_master/master_client.h"
#include "recordlab_master/master_server.h"
#include <cassert>
#include <chrono>
#include <thread>

int main() {
  recordlab::MasterServer server(5700, 5701, 1000);
  server.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  recordlab::MasterClient c("tcp://127.0.0.1:5700");
  assert(c.registerNode({{"node", "/test_node"}})["ok"]);
  assert(c.listNodes()["data"].size() == 1);
  assert(c.registerPublisher({{"node", "/test_node"}, {"topic", "/test/topic"}, {"transport", {{"type", "tcp_pubsub"}}}})["ok"]);
  assert(c.lookupTopic("/test/topic")["data"].size() == 1);
  server.stop();
  return 0;
}
