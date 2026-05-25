#include "recordlab_master/master_server.h"
#include "recordlab_echo/echo.h"
#include "recordlab_echo/shm_ring_buffer.h"
#include "recordlab_system_nodes/recorder/recorder_node.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

int main() {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "recordlab_recorder_node_contract";
  fs::remove_all(root);
  setenv("RECORDLAB_RECORD_ROOT", root.string().c_str(), 1);

  recordlab::MasterServer server(5830, 5831, 1000);
  server.start();

  recordlab::MasterClient client("tcp://127.0.0.1:5830");
  client.registerNode({{"node", "/fake_bsp_node"}, {"kind", "test_node"}});
  recordlab::ShmRingBuffer imu_ring;
  recordlab::ShmRingBuffer image_ring;
  assert(imu_ring.create("/recordlab_test_recorder_imu", 16, 4096));
  assert(image_ring.create("/recordlab_test_recorder_image", 4, 1024 * 1024));
  client.registerPublisher({{"node", "/fake_bsp_node"},
                            {"topic", "/bsp/imu"},
                            {"msg_type", "recordlab_msgs/ImuBatch"},
                            {"transport", {{"type", "shm_ring_buffer"},
                                            {"shm_name", "/recordlab_test_recorder_imu"},
                                            {"layout", "ring_buffer_v1"},
                                            {"slot_count", 16},
                                            {"slot_size", 4096}}}});
  client.registerPublisher({{"node", "/fake_bsp_node"},
                            {"topic", "/bsp/rgb/image_raw"},
                            {"msg_type", "recordlab_msgs/ImageFrame"},
                            {"transport", {{"type", "shm_ring_buffer"},
                                            {"shm_name", "/recordlab_test_recorder_image"},
                                            {"layout", "ring_buffer_v1"},
                                            {"slot_count", 4},
                                            {"slot_size", 1024 * 1024}}}});

  recordlab::nodes::system_nodes::recorder::RecorderNode recorder("tcp://127.0.0.1:5830");
  assert(recorder.start());

  auto action = client.lookupAction("/record/start")["data"]["endpoints"];
  recordlab::ActionClient start(action, 1000);
  auto goal_id = start.sendGoal({{"dataset_name", "free_record/imu_and_cam/test_dataset"},
                                 {"record_profile", "bsp_imu_cam"},
                                 {"topics", recordlab::json::array({"/bsp/imu", "/bsp/rgb/image_raw"})},
                                 {"metadata", {{"purpose", "contract"}}}});
  auto result = start.waitForResult(goal_id, 3000);
  assert(result["data"]["success"] == true);

  recordlab::ShmMessage imu;
  imu.timestamp_ns = 100;
  imu.encoding = 1;
  imu.sample_count = 1;
  std::string payload = R"({"items":[{"imu_idx":0,"gyro":[1,2,3]}]})";
  imu.payload.assign(payload.begin(), payload.end());
  assert(imu_ring.write(imu));

  recordlab::ShmMessage image;
  image.timestamp_ns = 200;
  image.encoding = 2;
  image.width = 2;
  image.height = 2;
  image.payload = {1, 2, 3, 4};
  assert(image_ring.write(image));
  std::this_thread::sleep_for(std::chrono::milliseconds(120));

  auto stop_action = client.lookupAction("/record/stop")["data"]["endpoints"];
  recordlab::ActionClient stop(stop_action, 1000);
  auto stop_goal = stop.sendGoal({});
  auto stop_result = stop.waitForResult(stop_goal, 3000);
  assert(stop_result["data"]["success"] == true);

  fs::path record_path = root / "free_record/imu_and_cam/test_dataset";
  assert(fs::exists(record_path / "record_request.json"));
  assert(fs::exists(record_path / "record_info.txt"));
  assert(fs::exists(record_path / "bsp_imu.jsonl"));
  assert(fs::exists(record_path / "bsp_rgb_image_raw_timestamps.txt"));
  bool saw_image = false;
  for (const auto &entry : fs::directory_iterator(record_path / "images")) {
    if (entry.is_regular_file()) saw_image = true;
  }
  assert(saw_image);

  recorder.stop();
  server.stop();
  fs::remove_all(root);
  unsetenv("RECORDLAB_RECORD_ROOT");
  return 0;
}
