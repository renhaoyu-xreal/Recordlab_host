#include "recordlab_host/data/camera_shared_memory.h"
#include "recordlab_host/common/logger.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace recordlab::host {
namespace {

constexpr int kCameraCount = 2;
constexpr int kSlotCount = 4;
constexpr int kMetaSize = 64;
constexpr int kSlotSize = 4 * 1024 * 1024;
constexpr std::size_t kSeqSize = kCameraCount * kSlotCount * sizeof(std::uint64_t);
constexpr std::size_t kTotalSize = kSeqSize + kCameraCount * kSlotCount * kSlotSize;

std::string normalizeName(const std::string& name) {
    if (name.empty()) {
        return "/recordlab_camera_shm_v1";
    }
    return name.front() == '/' ? name : "/" + name;
}

std::uint32_t readU32(const char* ptr) {
    std::uint32_t value = 0;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

std::uint64_t readU64(const char* ptr) {
    std::uint64_t value = 0;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

}  // namespace

CameraSharedMemoryReader::~CameraSharedMemoryReader() {
    detach();
}

bool CameraSharedMemoryReader::attach(const std::string& shm_name) {
    const std::string normalized = normalizeName(shm_name);
    if (mapping_ && shm_name_ == normalized) {
        return true;
    }

    detach();
    fd_ = ::shm_open(normalized.c_str(), O_RDONLY, 0600);
    if (fd_ < 0) {
        return false;
    }

    struct stat st {};
    if (::fstat(fd_, &st) != 0 || st.st_size < static_cast<off_t>(kTotalSize)) {
        common::Logger::instance().log(
            common::LogLevel::Warn,
            "CameraSharedMemoryReader",
            "invalid shared memory size for " + normalized);
        detach();
        return false;
    }

    void* mapped = ::mmap(nullptr, kTotalSize, PROT_READ, MAP_SHARED, fd_, 0);
    if (mapped == MAP_FAILED) {
        detach();
        return false;
    }

    mapping_ = static_cast<const char*>(mapped);
    mapping_size_ = kTotalSize;
    shm_name_ = normalized;
    common::Logger::instance().log(
        common::LogLevel::Info,
        "CameraSharedMemoryReader",
        "attached " + normalized);
    return true;
}

void CameraSharedMemoryReader::detach() {
    if (mapping_) {
        ::munmap(const_cast<char*>(mapping_), mapping_size_);
    }
    mapping_ = nullptr;
    mapping_size_ = 0;
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = -1;
    shm_name_.clear();
}

bool CameraSharedMemoryReader::readLatestFrame(const std::string& shm_name,
                                               int camera_index,
                                               std::uint64_t& last_seq,
                                               CameraSharedFrame& frame) {
    if (camera_index < 0 || camera_index >= kCameraCount) {
        return false;
    }
    if (!attach(shm_name)) {
        return false;
    }

    int latest_slot = -1;
    std::uint64_t latest_seq = last_seq;
    const std::size_t seq_base = static_cast<std::size_t>(camera_index) * kSlotCount * sizeof(std::uint64_t);
    for (int slot = 0; slot < kSlotCount; ++slot) {
        const auto seq = readU64(mapping_ + seq_base + static_cast<std::size_t>(slot) * sizeof(std::uint64_t));
        if (seq > latest_seq) {
            latest_seq = seq;
            latest_slot = slot;
        }
    }
    if (latest_slot < 0) {
        return false;
    }

    const std::size_t slot_offset = kSeqSize
        + (static_cast<std::size_t>(camera_index) * kSlotCount + static_cast<std::size_t>(latest_slot)) * kSlotSize;
    if (slot_offset + kMetaSize > mapping_size_) {
        return false;
    }
    const char* slot_base = mapping_ + slot_offset;
    CameraSharedFrame candidate;
    candidate.width = static_cast<int>(readU32(slot_base + 0));
    candidate.height = static_cast<int>(readU32(slot_base + 4));
    candidate.format = static_cast<int>(readU32(slot_base + 8));
    candidate.data_size = static_cast<int>(readU32(slot_base + 12));
    candidate.bytes_per_line = static_cast<int>(readU32(slot_base + 16));
    candidate.encoding = static_cast<int>(readU32(slot_base + 20));
    candidate.seq = latest_seq;

    if (candidate.width <= 0 || candidate.height <= 0 || candidate.data_size <= 0 || candidate.bytes_per_line <= 0) {
        return false;
    }
    if (kMetaSize + candidate.data_size > kSlotSize) {
        return false;
    }
    if (slot_offset + kMetaSize + static_cast<std::size_t>(candidate.data_size) > mapping_size_) {
        return false;
    }

    candidate.data = QByteArray(slot_base + kMetaSize, candidate.data_size);

    const auto verify_seq = readU64(mapping_ + seq_base + static_cast<std::size_t>(latest_slot) * sizeof(std::uint64_t));
    if (verify_seq != latest_seq) {
        return false;
    }

    last_seq = latest_seq;
    frame = std::move(candidate);
    return true;
}

}  // namespace recordlab::host
