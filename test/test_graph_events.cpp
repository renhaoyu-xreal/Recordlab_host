#include "recordlab_core/master_client.h"
#include "recordlab_master/master_server.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <zmq.hpp>

int main() {
  recordlab::MasterServer server(5710, 5711, 1000);
  server.start();
  zmq::context_t ctx(1);
  zmq::socket_t sub(ctx, zmq::socket_type::sub);
  sub.set(zmq::sockopt::subscribe, "");
  sub.set(zmq::sockopt::rcvtimeo, 1000);
  sub.connect("tcp://127.0.0.1:5711");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  recordlab::MasterClient c("tcp://127.0.0.1:5710");
  c.registerNode({{"node", "/graph_node"}});
  zmq::message_t msg;
  auto ok = sub.recv(msg, zmq::recv_flags::none);
  assert(ok);
  auto j = recordlab::json::parse(std::string(static_cast<char *>(msg.data()), msg.size()));
  assert(j["event"] == "node_added");
  server.stop();
  return 0;
}
