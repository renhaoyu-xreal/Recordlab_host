#include "recordlab_master/shm_ring_buffer.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace recordlab {

struct ShmRingBuffer::Header {
  uint32_t magic;
  uint32_t version;
  uint32_t slot_count;
  uint32_t slot_size;
  std::atomic<uint64_t> write_seq;
};

struct ShmRingBuffer::SlotHeader {
  std::atomic<uint64_t> seq_begin;
  uint64_t timestamp_ns;
  uint32_t payload_size;
  uint32_t encoding;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t sample_count;
  std::atomic<uint64_t> seq_end;
};

static constexpr uint32_t kMagic = 0x524C5348;  // RLSH
static constexpr uint32_t kVersion = 1;

ShmRingBuffer::~ShmRingBuffer() { close(owner_); }

size_t ShmRingBuffer::totalSize(uint32_t slots, uint32_t slot_size) const {
  return sizeof(Header) + static_cast<size_t>(slots) * slot_size;
}

bool ShmRingBuffer::create(const std::string &name, uint32_t slot_count, uint32_t slot_size) {
  close(owner_);
  name_ = name.front() == '/' ? name : "/" + name;
  shm_unlink(name_.c_str());
  fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0600);
  if (fd_ < 0) return false;
  mapped_size_ = totalSize(slot_count, slot_size);
  if (ftruncate(fd_, mapped_size_) != 0) return false;
  base_ = static_cast<uint8_t *>(mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
  if (base_ == MAP_FAILED) {
    base_ = nullptr;
    return false;
  }
  std::memset(base_, 0, mapped_size_);
  auto *h = reinterpret_cast<Header *>(base_);
  h->magic = kMagic;
  h->version = kVersion;
  h->slot_count = slot_count;
  h->slot_size = slot_size;
  h->write_seq.store(0, std::memory_order_release);
  owner_ = true;
  return true;
}

bool ShmRingBuffer::attach(const std::string &name) {
  close(false);
  name_ = name.front() == '/' ? name : "/" + name;
  fd_ = shm_open(name_.c_str(), O_RDWR, 0600);
  if (fd_ < 0) return false;
  Header h{};
  if (::read(fd_, &h, sizeof(h)) != static_cast<ssize_t>(sizeof(h))) return false;
  if (h.magic != kMagic || h.version != kVersion) return false;
  mapped_size_ = totalSize(h.slot_count, h.slot_size);
  base_ = static_cast<uint8_t *>(mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
  if (base_ == MAP_FAILED) {
    base_ = nullptr;
    return false;
  }
  owner_ = false;
  return true;
}

void ShmRingBuffer::close(bool unlink_segment) {
  if (base_) {
    munmap(base_, mapped_size_);
    base_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (unlink_segment && !name_.empty()) shm_unlink(name_.c_str());
  mapped_size_ = 0;
  owner_ = false;
}

uint64_t ShmRingBuffer::writeSeq() const {
  if (!base_) return 0;
  return reinterpret_cast<Header *>(base_)->write_seq.load(std::memory_order_acquire);
}

uint32_t ShmRingBuffer::slotCount() const {
  return base_ ? reinterpret_cast<Header *>(base_)->slot_count : 0;
}

uint32_t ShmRingBuffer::slotSize() const {
  return base_ ? reinterpret_cast<Header *>(base_)->slot_size : 0;
}

uint8_t *ShmRingBuffer::slotBase(uint64_t seq) const {
  auto *h = reinterpret_cast<Header *>(base_);
  const uint64_t idx = seq % h->slot_count;
  return base_ + sizeof(Header) + idx * h->slot_size;
}

bool ShmRingBuffer::write(const ShmMessage &message) {
  if (!base_) return false;
  auto *h = reinterpret_cast<Header *>(base_);
  if (sizeof(SlotHeader) + message.payload.size() > h->slot_size) return false;
  const uint64_t seq = h->write_seq.load(std::memory_order_relaxed) + 1;
  auto *slot = slotBase(seq);
  auto *sh = reinterpret_cast<SlotHeader *>(slot);
  sh->seq_begin.store(seq, std::memory_order_release);
  sh->timestamp_ns = message.timestamp_ns;
  sh->payload_size = static_cast<uint32_t>(message.payload.size());
  sh->encoding = message.encoding;
  sh->width = message.width;
  sh->height = message.height;
  sh->stride = message.stride;
  sh->sample_count = message.sample_count;
  std::memcpy(slot + sizeof(SlotHeader), message.payload.data(), message.payload.size());
  sh->seq_end.store(seq, std::memory_order_release);
  h->write_seq.store(seq, std::memory_order_release);
  return true;
}

ShmReadResult ShmRingBuffer::readLatest() const {
  return readNext(writeSeq());
}

ShmReadResult ShmRingBuffer::readNext(uint64_t expected_seq) const {
  ShmReadResult out;
  out.expected_seq = expected_seq;
  if (!base_ || expected_seq == 0) return out;
  auto *h = reinterpret_cast<Header *>(base_);
  const uint64_t latest = h->write_seq.load(std::memory_order_acquire);
  if (latest == 0) return out;
  uint64_t seq = expected_seq;
  if (seq + h->slot_count <= latest) {
    out.gap = true;
    seq = latest;
  }
  if (seq > latest) return out;
  auto *slot = slotBase(seq);
  auto *sh = reinterpret_cast<SlotHeader *>(slot);
  const uint64_t begin = sh->seq_begin.load(std::memory_order_acquire);
  const uint64_t end = sh->seq_end.load(std::memory_order_acquire);
  if (begin != seq || end != seq) {
    out.gap = true;
    return readLatest();
  }
  out.ok = true;
  out.message.seq = seq;
  out.message.timestamp_ns = sh->timestamp_ns;
  out.message.encoding = sh->encoding;
  out.message.width = sh->width;
  out.message.height = sh->height;
  out.message.stride = sh->stride;
  out.message.sample_count = sh->sample_count;
  out.message.payload.resize(sh->payload_size);
  std::memcpy(out.message.payload.data(), slot + sizeof(SlotHeader), sh->payload_size);
  return out;
}

}  // namespace recordlab
