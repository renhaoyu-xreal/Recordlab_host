#include "recordlab_system_nodes/recorder/recorder_node.h"

#include "recordlab_master/registries.h"

#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace recordlab::nodes::system_nodes::recorder {

namespace {

std::string safeName(const std::string &topic) {
  std::string out;
  for (char c : topic) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out.push_back(c);
    } else if (!out.empty() && out.back() != '_') {
      out.push_back('_');
    }
  }
  while (!out.empty() && out.front() == '_') out.erase(out.begin());
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out.empty() ? "topic" : out;
}

void writeText(const std::filesystem::path &path, const std::string &text) {
  std::ofstream out(path);
  out << text;
}

}  // namespace

RecorderNode::RecorderNode(std::string endpoint)
    : NodeBase("/recorder_node", "/record", std::move(endpoint)) {}

RecorderNode::~RecorderNode() { stop(); }

bool RecorderNode::start() {
  if (!NodeBase::start()) return false;
  status_pub_ = std::make_unique<Publisher>("/record/status");
  start_action_ = std::make_unique<ActionServer>(
      [this](const json &goal, std::function<void(const json &)>, std::atomic<bool> &) {
        return startRecord(goal);
      });
  stop_action_ = std::make_unique<ActionServer>(
      [this](const json &goal, std::function<void(const json &)>, std::atomic<bool> &) {
        return stopRecord(goal);
      });
  status_service_ = std::make_unique<ServiceServer>([this](const json &) { return status(); });

  client_.registerPublisher({{"node", node_name_},
                             {"topic", "/record/status"},
                             {"msg_type", "recordlab_msgs/RecordStatus"},
                             {"transport", {{"type", "tcp_pubsub"},
                                             {"endpoint", status_pub_->endpoint()}}}});
  client_.registerAction({{"node", node_name_},
                          {"action", "/record/start"},
                          {"endpoints", start_action_->descriptor()}});
  client_.registerAction({{"node", node_name_},
                          {"action", "/record/stop"},
                          {"endpoints", stop_action_->descriptor()}});
  client_.registerService({{"node", node_name_},
                           {"service", "/record/status"},
                           {"endpoint", status_service_->endpoint()}});
  publishStatus();
  return true;
}

void RecorderNode::stop() {
  if (recording_) {
    stopRecord(json::object());
  }
  NodeBase::stop();
}

json RecorderNode::startRecord(const json &goal) {
  std::lock_guard<std::mutex> lock(mu_);
  if (recording_) {
    return {{"success", false}, {"message", "RecorderNode 已在录制"}, {"dataset_name", current_dataset_}};
  }
  const std::string dataset = goal.value("dataset_name", "");
  if (dataset.empty()) return {{"success", false}, {"message", "缺少 dataset_name"}};
  const json topics = goal.value("topics", json::array());
  if (!topics.is_array() || topics.empty()) {
    return {{"success", false}, {"message", "缺少需要录制的 topic 列表"}};
  }

  std::vector<std::unique_ptr<TopicReader>> readers;
  json attached = json::array();
  for (const auto &topic : topics) {
    if (!topic.is_string()) continue;
    attachTopic(topic.get<std::string>(), readers, attached);
  }
  if (readers.empty()) {
    return {{"success", false}, {"message", "没有可录制的 shm_ring_buffer topic"}};
  }

  current_dataset_ = dataset;
  current_profile_ = goal.value("record_profile", "");
  current_metadata_ = goal.value("metadata", json::object());
  current_path_ = recordRoot() / dataset;
  std::filesystem::create_directories(current_path_);
  std::filesystem::create_directories(current_path_ / "images");
  writeText(current_path_ / "record_request.json", goal.dump(2));
  writeText(current_path_ / "topics.json", attached.dump(2));
  seq_gap_count_ = 0;
  record_start_ms_ = nowMs();
  readers_ = std::move(readers);
  recording_ = true;
  record_thread_ = std::thread(&RecorderNode::recordLoop, this);
  return {{"success", true},
          {"message", "RecorderNode 录制已开始"},
          {"dataset_name", current_dataset_},
          {"record_profile", current_profile_},
          {"record_path", current_path_.string()},
          {"topics", attached}};
}

json RecorderNode::stopRecord(const json &) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!recording_) return {{"success", false}, {"message", "RecorderNode 未在录制"}};
    recording_ = false;
  }
  if (record_thread_.joinable()) record_thread_.join();

  std::lock_guard<std::mutex> lock(mu_);
  const double seconds = (nowMs() - record_start_ms_) / 1000.0;
  writeText(current_path_ / "record_info.txt",
            "recordlab_version=v1\n"
            "node=/recorder_node\n"
            "record_profile=" + current_profile_ + "\n"
            "dataset_name=" + current_dataset_ + "\n"
            "duration_s=" + std::to_string(seconds) + "\n"
            "seq_gap_count=" + std::to_string(seq_gap_count_.load()) + "\n");
  readers_.clear();
  return {{"success", true},
          {"message", "RecorderNode 录制已停止"},
          {"dataset_name", current_dataset_},
          {"record_path", current_path_.string()},
          {"record_timer", seconds},
          {"seq_gap_count", seq_gap_count_.load()}};
}

json RecorderNode::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return {{"recording", recording_.load()},
          {"dataset_name", current_dataset_},
          {"record_profile", current_profile_},
          {"record_path", current_path_.string()},
          {"seq_gap_count", seq_gap_count_.load()},
          {"timestamp_ms", nowMs()}};
}

void RecorderNode::publishStatus() {
  if (status_pub_) status_pub_->publish(status());
}

bool RecorderNode::attachTopic(const std::string &topic, std::vector<std::unique_ptr<TopicReader>> &readers, json &attached) {
  auto lookup = client_.lookupTopic(topic);
  if (!lookup.value("ok", false) || !lookup["data"].is_array()) return false;
  for (const auto &entry : lookup["data"]) {
    const auto transport = entry.value("transport", json::object());
    if (transport.value("type", "") != "shm_ring_buffer") continue;
    const std::string shm_name = transport.value("shm_name", "");
    if (shm_name.empty()) continue;
    auto reader = std::make_unique<TopicReader>();
    reader->topic = entry.value("topic", topic);
    reader->msg_type = entry.value("msg_type", "");
    reader->safe_name = safeName(reader->topic);
    if (!reader->ring.attach(shm_name)) continue;
    reader->next_seq = reader->ring.writeSeq() + 1;
    attached.push_back({{"topic", reader->topic},
                        {"msg_type", reader->msg_type},
                        {"shm_name", shm_name},
                        {"safe_name", reader->safe_name}});
    readers.push_back(std::move(reader));
    return true;
  }
  return false;
}

void RecorderNode::recordLoop() {
  while (recording_) {
    std::vector<TopicReader *> readers;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto &reader : readers_) readers.push_back(reader.get());
    }
    for (auto *reader : readers) {
      for (int i = 0; i < 32; ++i) {
        const auto result = reader->ring.readNext(reader->next_seq);
        if (!result.ok) break;
        if (result.gap) ++seq_gap_count_;
        writeMessage(*reader, result.message);
        reader->next_seq = result.message.seq + 1;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void RecorderNode::writeMessage(const TopicReader &reader, const ShmMessage &message) {
  const bool image_like = reader.topic.find("image") != std::string::npos || message.encoding == 2;
  if (image_like) {
    std::ostringstream name;
    name << reader.safe_name << "_" << message.timestamp_ns << "_" << std::setw(8)
         << std::setfill('0') << message.seq << ".bin";
    const auto image_path = current_path_ / "images" / name.str();
    std::ofstream image(image_path, std::ios::binary);
    image.write(reinterpret_cast<const char *>(message.payload.data()), static_cast<std::streamsize>(message.payload.size()));
    std::ofstream ts(current_path_ / (reader.safe_name + "_timestamps.txt"), std::ios::app);
    ts << message.timestamp_ns << " " << name.str() << " " << message.payload.size() << "\n";
    return;
  }

  std::ofstream out(current_path_ / (reader.safe_name + ".jsonl"), std::ios::app);
  out << json{{"seq", message.seq},
              {"timestamp_ns", message.timestamp_ns},
              {"encoding", message.encoding},
              {"sample_count", message.sample_count},
              {"payload", std::string(message.payload.begin(), message.payload.end())}}
             .dump()
      << "\n";
}

std::filesystem::path RecorderNode::recordRoot() const {
  const char *env = std::getenv("RECORDLAB_RECORD_ROOT");
  if (env && *env) return std::filesystem::path(env);
  return std::filesystem::path(RECORDLAB_MASTER_SOURCE_DIR) / "data";
}

}  // namespace recordlab::nodes::system_nodes::recorder
