#include "recordlab_host/data/camera_shared_memory.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kHeaderMagic = 0x52434d48;
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kCameraCount = 2;
constexpr std::uint32_t kSlotCount = 4;
constexpr std::uint32_t kHeaderSize = 64;
constexpr std::uint32_t kMetaSize = 64;
constexpr std::uint32_t kSlotSize = 4096;
constexpr std::size_t kSeqSize = kCameraCount * kSlotCount * sizeof(std::uint64_t);
constexpr std::size_t kTotalSize = kHeaderSize + kSeqSize + kCameraCount * kSlotCount * kSlotSize;
constexpr const char* kShmName = "/recordlab_test_camera_shm_recreate";

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

struct SharedMemoryHandle {
    int fd = -1;
    void* mapping = MAP_FAILED;
};

SharedMemoryHandle createSharedMemory() {
    ::shm_unlink(kShmName);
    SharedMemoryHandle handle;
    handle.fd = ::shm_open(kShmName, O_CREAT | O_EXCL | O_RDWR, 0600);
    require(handle.fd >= 0, "failed to create shm");
    require(::ftruncate(handle.fd, static_cast<off_t>(kTotalSize)) == 0, "failed to resize shm");
    handle.mapping = ::mmap(nullptr, kTotalSize, PROT_READ | PROT_WRITE, MAP_SHARED, handle.fd, 0);
    require(handle.mapping != MAP_FAILED, "failed to mmap shm");
    std::memset(handle.mapping, 0, kTotalSize);
    return handle;
}

void destroySharedMemory(SharedMemoryHandle& handle, bool unlink_name) {
    if (handle.mapping != MAP_FAILED) {
        ::munmap(handle.mapping, kTotalSize);
        handle.mapping = MAP_FAILED;
    }
    if (handle.fd >= 0) {
        ::close(handle.fd);
        handle.fd = -1;
    }
    if (unlink_name) {
        ::shm_unlink(kShmName);
    }
}

void writeFrame(const SharedMemoryHandle& handle,
                std::uint64_t seq,
                const std::vector<char>& payload,
                std::uint32_t width,
                std::uint32_t height,
                std::uint32_t bytes_per_line,
                std::uint32_t encoding) {
    auto* base = static_cast<char*>(handle.mapping);
    std::memset(base, 0, kTotalSize);
    std::uint32_t header[] = {
        kHeaderMagic,
        kVersion,
        kCameraCount,
        kSlotCount,
        kSlotSize,
        kMetaSize,
    };
    std::memcpy(base, header, sizeof(header));

    const std::size_t seq_offset = kHeaderSize;
    std::memcpy(base + seq_offset, &seq, sizeof(seq));

    const std::size_t slot_offset = kHeaderSize + kSeqSize;
    std::uint32_t meta[] = {
        width,
        height,
        bytes_per_line == width ? 24u : 13u,
        static_cast<std::uint32_t>(payload.size()),
        bytes_per_line,
        encoding,
    };
    std::memcpy(base + slot_offset, meta, sizeof(meta));
    std::memcpy(base + slot_offset + kMetaSize, payload.data(), payload.size());
}

}  // namespace

int main() {
    SharedMemoryHandle first = createSharedMemory();
    writeFrame(first, 1, {'a', 'b', 'c', 'd'}, 2, 2, 2, 0);

    recordlab::host::CameraSharedMemoryReader reader;
    std::uint64_t last_seq = 0;
    recordlab::host::CameraSharedFrame frame;
    require(reader.readLatestFrame(kShmName, 0, last_seq, frame), "failed to read first frame");
    require(last_seq == 1, "unexpected first seq");
    require(frame.data.size() == 4, "unexpected first payload size");
    require(frame.data[0] == 'a', "unexpected first payload content");

    destroySharedMemory(first, true);

    SharedMemoryHandle second = createSharedMemory();
    writeFrame(second, 5, {'n', 'e', 'w', '!'}, 2, 2, 2, 0);

    recordlab::host::CameraSharedFrame recreated_frame;
    require(reader.readLatestFrame(kShmName, 0, last_seq, recreated_frame), "failed to read recreated shm frame");
    require(last_seq == 5, "reader did not observe recreated shm seq");
    require(recreated_frame.data.size() == 4, "unexpected recreated payload size");
    require(recreated_frame.data[0] == 'n', "reader did not remap recreated shm");

    destroySharedMemory(second, true);
    return 0;
}
