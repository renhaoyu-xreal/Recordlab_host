#include "recordlab_core/master_client.h"
#include "recordlab_core/user_log.h"
#include "recordlab_echo/echo.h"
#include "recordlab_master/master_server.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "recordlab_user_log_contract";
  fs::remove_all(root);
  setenv("RECORDLAB_LOG_ROOT", root.string().c_str(), 1);

  recordlab::MasterServer server(5870, 5871, 1000);
  server.start();

  recordlab::MasterClient client("tcp://127.0.0.1:5870");
  client.registerNode({{"node", "/logger_test"}, {"kind", "test_node"}});
  recordlab::UserLogPublisher logs("/logger_test");
  logs.registerPublisher(client, "/logger_test");

  auto topic = client.lookupTopic("/recordlab/user_log");
  assert(topic["ok"] == true);
  assert(topic["data"][0]["msg_type"] == "recordlab_msgs/UserLog");

  recordlab::json seen = recordlab::json::array();
  std::mutex mu;
  recordlab::Subscriber sub(
      topic["data"][0]["transport"]["endpoint"], "/recordlab/user_log",
      [&](const recordlab::json &msg) {
        std::lock_guard<std::mutex> lock(mu);
        seen.push_back(msg);
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  logs.info("setup", "info message");
  logs.warn("setup", "warn message");
  logs.error("setup", "error message", {{"code", 42}});

  for (int i = 0; i < 50; ++i) {
    {
      std::lock_guard<std::mutex> lock(mu);
      if (seen.size() >= 3) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  std::lock_guard<std::mutex> lock(mu);
  assert(seen.size() >= 3);
  assert(seen[0]["level"] == "INFO");
  assert(seen[1]["level"] == "WARN");
  assert(seen[2]["level"] == "ERROR");
  assert(seen[2]["details"]["code"] == 42);

  server.stop();
  bool saw_log_file = false;
  for (const auto &dir : fs::directory_iterator(root)) {
    if (!dir.is_directory()) continue;
    const auto path = dir.path() / "user" / "logger_test.log";
    if (fs::exists(path) && fs::file_size(path) > 0) saw_log_file = true;
  }
  assert(saw_log_file);
  unsetenv("RECORDLAB_LOG_ROOT");
  fs::remove_all(root);
  return 0;
}
