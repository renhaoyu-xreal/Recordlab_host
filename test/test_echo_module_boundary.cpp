#include "recordlab_echo/action.h"
#include "recordlab_echo/endpoint.h"
#include "recordlab_echo/pubsub.h"
#include "recordlab_echo/rpc.h"
#include "recordlab_echo/shm_ring_buffer.h"
#include "recordlab_echo/stdio_channel.h"
#include "recordlab_echo/wire_protocol.h"

#include <cassert>

int main() {
  recordlab::ServiceServer server([](const recordlab::json &request) {
    return recordlab::json{{"success", true}, {"echo", request}};
  });
  recordlab::ServiceClient client(server.endpoint(), 1000);
  auto reply = client.call({{"value", 42}});
  assert(reply["ok"] == true);
  assert(reply["data"]["success"] == true);
  assert(reply["data"]["echo"]["value"] == 42);

  recordlab::ActionServer action([](const recordlab::json &goal,
                                    std::function<void(const recordlab::json &)>,
                                    std::atomic<bool> &) {
    return recordlab::json{{"success", true}, {"goal", goal}};
  });
  recordlab::ActionClient action_client(action.descriptor(), 1000);
  const auto id = action_client.sendGoal({{"name", "module-boundary"}});
  auto result = action_client.waitForResult(id, 2000);
  assert(result["ok"] == true);
  assert(result["data"]["success"] == true);
  assert(result["data"]["goal"]["name"] == "module-boundary");

  return 0;
}
