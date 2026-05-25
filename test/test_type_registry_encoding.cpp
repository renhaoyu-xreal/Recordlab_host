#include "recordlab_master/registries.h"

#include <cassert>
#include <stdexcept>

int main() {
  recordlab::RegistryStore store;

  store.registerType({{"type_name", "recordlab_msgs/Status"},
                      {"version", "1"},
                      {"encoding", "json"},
                      {"schema_hash", "abc"},
                      {"schema_uri", "recordlab://schemas/status/v1"},
                      {"description", "低频状态消息"}});
  auto status = store.lookupType("recordlab_msgs/Status");
  assert(status["encoding"] == "json");
  assert(status["schema_uri"] == "recordlab://schemas/status/v1");

  store.registerType({{"type_name", "recordlab_msgs/ImageFrame"},
                      {"encoding", "shm_ring_buffer"}});
  auto image = store.lookupType("recordlab_msgs/ImageFrame");
  assert(image["version"] == "1");
  assert(image["schema_hash"] == "");
  assert(image["encoding"] == "shm_ring_buffer");

  store.registerType({{"type_name", "recordlab_msgs/FutureProto"},
                      {"encoding", "protobuf_reserved"}});
  assert(store.lookupType("recordlab_msgs/FutureProto")["encoding"] == "protobuf_reserved");

  bool rejected = false;
  try {
    store.registerType({{"type_name", "bad"}, {"encoding", "protobuf"}});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  return 0;
}
