#include "recordlab_master/transport.h"
#include <cassert>
#include <chrono>
#include <thread>

int main() {
  recordlab::ServiceClient bad("tcp://127.0.0.1:5999", 100);
  try { bad.call({{"first", true}}); } catch (...) {}

  recordlab::ServiceServer server([](const recordlab::json &req) {
    return recordlab::json{{"echo", req}};
  });
  recordlab::ServiceClient good(server.endpoint(), 500);
  auto resp = good.call({{"second", true}});
  assert(resp["ok"] == true);

  recordlab::ActionServer action([](const recordlab::json &goal, auto feedback, std::atomic<bool> &) {
    feedback({{"progress", 1}});
    return recordlab::json{{"done", goal}};
  });
  recordlab::ActionClient client(action.descriptor(), 500);
  auto id = client.sendGoal({{"x", 1}});
  auto result = client.waitForResult(id, 2000);
  assert(result["ok"] == true);
  return 0;
}
