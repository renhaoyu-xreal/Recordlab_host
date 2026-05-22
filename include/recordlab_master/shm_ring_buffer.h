#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace recordlab {

struct ShmMessage {
  uint64_t seq{0};
  uint64_t timestamp_ns{0};
  uint32_t encoding{0};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t stride{0};
  uint32_t sample_count{0};
  std::vector<uint8_t> payload;
};

struct ShmReadResult {
  bool ok{false};
  bool gap{false};
  uint64_t expected_seq{0};
  ShmMessage message;
};

class ShmRingBuffer {
 public:
  ShmRingBuffer() = default;
  ~ShmRingBuffer();
  ShmRingBuffer(const ShmRingBuffer &) = delete;
  ShmRingBuffer &operator=(const ShmRingBuffer &) = delete;

  bool create(const std::string &name, uint32_t slot_count, uint32_t slot_size);
  bool attach(const std::string &name);
  void close(bool unlink_segment = false);

  bool write(const ShmMessage &message);
  ShmReadResult readLatest() const;
  ShmReadResult readNext(uint64_t expected_seq) const;

  uint64_t writeSeq() const;
  uint32_t slotCount() const;
  uint32_t slotSize() const;
  std::string name() const { return name_; }

 private:
  struct Header;
  struct SlotHeader;
  size_t totalSize(uint32_t slots, uint32_t slot_size) const;
  uint8_t *slotBase(uint64_t seq) const;

  std::string name_;
  int fd_{-1};
  uint8_t *base_{nullptr};
  size_t mapped_size_{0};
  bool owner_{false};
};

}  // namespace recordlab
