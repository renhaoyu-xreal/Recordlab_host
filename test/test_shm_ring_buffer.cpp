#include "recordlab_echo/shm_ring_buffer.h"
#include <cassert>

int main() {
  recordlab::ShmRingBuffer writer;
  assert(writer.create("/recordlab_test_shm", 2, 1024));
  recordlab::ShmRingBuffer reader;
  assert(reader.attach("/recordlab_test_shm"));
  recordlab::ShmMessage msg;
  msg.timestamp_ns = 10;
  msg.sample_count = 1;
  msg.payload = {1, 2, 3};
  assert(writer.write(msg));
  auto r = reader.readNext(1);
  assert(r.ok && r.message.payload.size() == 3);
  writer.write(msg);
  writer.write(msg);
  writer.write(msg);
  auto gap = reader.readNext(1);
  assert(gap.ok && gap.gap);
  writer.close(true);
  return 0;
}
