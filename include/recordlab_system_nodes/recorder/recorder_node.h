#pragma once

#include "recordlab_echo/echo.h"
#include "recordlab_echo/shm_ring_buffer.h"
#include "recordlab_core/node_base.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace recordlab::nodes::system_nodes::recorder {

class RecorderNode : public NodeBase {
 public:
  explicit RecorderNode(std::string endpoint = "tcp://127.0.0.1:5590");
  ~RecorderNode() override;

  bool start() override;
  void stop() override;

 private:
  struct TopicReader {
    std::string topic;
    std::string msg_type;
    std::string safe_name;
    ShmRingBuffer ring;
    uint64_t next_seq{1};
  };

  json startRecord(const json &goal);
  json stopRecord(const json &goal);
  json status() const;
  void recordLoop();
  void publishStatus();
  bool attachTopic(const std::string &topic, std::vector<std::unique_ptr<TopicReader>> &readers, json &attached);
  void writeMessage(const TopicReader &reader, const ShmMessage &message);
  std::filesystem::path recordRoot() const;

  std::unique_ptr<Publisher> status_pub_;
  std::unique_ptr<ActionServer> start_action_;
  std::unique_ptr<ActionServer> stop_action_;
  std::unique_ptr<ServiceServer> status_service_;

  std::atomic<bool> recording_{false};
  std::thread record_thread_;
  mutable std::mutex mu_;
  std::vector<std::unique_ptr<TopicReader>> readers_;
  std::filesystem::path current_path_;
  std::string current_dataset_;
  std::string current_profile_;
  json current_metadata_{json::object()};
  int64_t record_start_ms_{0};
  std::atomic<uint64_t> seq_gap_count_{0};
};

}  // namespace recordlab::nodes::system_nodes::recorder
