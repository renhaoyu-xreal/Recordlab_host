#include "recordlab_core/master_client.h"
#include "recordlab_master/master_server.h"
#include "recordlab_core/script_runner.h"
#include "recordlab_echo/echo.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

int main() {
  recordlab::MasterServer server(5720, 5721, 1000);
  server.start();

  const std::filesystem::path script = std::filesystem::temp_directory_path() / "recordlab_runner_test.py";
  {
    std::ofstream out(script);
    out << "import json\n";
    out << "print('hello from script')\n";
    out << "print('RECORDLAB_EVENT_JSON ' + json.dumps({'type':'workflow','action':'state','title':'测试流程','steps':[],'message':'ok','finished':False,'success':None}, ensure_ascii=False))\n";
  }

  recordlab::ScriptRunner runner("tcp://127.0.0.1:5720");
  assert(runner.start());

  recordlab::MasterClient client("tcp://127.0.0.1:5720");
  auto nodes = client.listNodes();
  assert(nodes["ok"] == true);
  assert(nodes["data"].size() == 1);

  auto status_topic = client.lookupTopic("/script_runner/status");
  assert(status_topic["ok"] == true);
  assert(status_topic["data"].size() == 1);
  assert(status_topic["data"][0]["transport"].contains("endpoint"));

  bool saw_log = false;
  bool saw_user_log = false;
  bool saw_workflow = false;
  recordlab::Subscriber log_sub(
      client.lookupTopic("/script_runner/log")["data"][0]["transport"]["endpoint"],
      "/script_runner/log",
      [&saw_log](const recordlab::json &msg) {
        if (msg.value("message", "").find("hello from script") != std::string::npos) saw_log = true;
      });
  recordlab::Subscriber user_log_sub(
      client.lookupTopic("/recordlab/user_log")["data"][0]["transport"]["endpoint"],
      "/recordlab/user_log",
      [&saw_user_log](const recordlab::json &msg) {
        if (msg.value("level", "") == "INFO" &&
            msg.value("message", "").find("hello from script") != std::string::npos) {
          saw_user_log = true;
        }
      });
  recordlab::Subscriber workflow_sub(
      client.lookupTopic("/script_runner/workflow")["data"][0]["transport"]["endpoint"],
      "/script_runner/workflow",
      [&saw_workflow](const recordlab::json &msg) {
        if (msg.value("message", "") == "ok") saw_workflow = true;
      });

  auto action = client.lookupAction("/script_runner/run_script");
  assert(action["ok"] == true);
  recordlab::ActionClient action_client(action["data"]["endpoints"], 1000);
  auto goal_id = action_client.sendGoal({{"script_path", script.string()}, {"args", recordlab::json::array()}});
  auto result = action_client.waitForResult(goal_id, 5000);
  assert(result["ok"] == true);
  assert(result["data"]["success"] == true);

  for (int i = 0; i < 50 && (!saw_log || !saw_user_log || !saw_workflow); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  assert(saw_log);
  assert(saw_user_log);
  assert(saw_workflow);

  auto stop = client.lookupService("/script_runner/stop_script");
  assert(stop["ok"] == true);
  recordlab::ServiceClient stop_client(stop["data"]["endpoint"], 1000);
  auto stop_resp = stop_client.call({{"reason", "test"}});
  assert(stop_resp["ok"] == true);

  runner.stop();
  server.stop();
  std::filesystem::remove(script);
  return 0;
}
