#pragma once

#include <QByteArray>

#include <array>
#include <cstdint>
#include <string>

namespace recordlab::host {

struct CameraSharedFrame {
    int width = 0;
    int height = 0;
    int format = 0;
    int data_size = 0;
    int bytes_per_line = 0;
    int encoding = 0;
    std::uint64_t seq = 0;
    QByteArray data;
};

class CameraSharedMemoryReader {
public:
    CameraSharedMemoryReader() = default;
    ~CameraSharedMemoryReader();

    CameraSharedMemoryReader(const CameraSharedMemoryReader&) = delete;
    CameraSharedMemoryReader& operator=(const CameraSharedMemoryReader&) = delete;

    bool readLatestFrame(const std::string& shm_name,
                         int camera_index,
                         std::uint64_t& last_seq,
                         CameraSharedFrame& frame);
    void detach();

private:
    bool attach(const std::string& shm_name);

    int fd_ = -1;
    const char* mapping_ = nullptr;
    std::size_t mapping_size_ = 0;
    std::string shm_name_;
};

}  // namespace recordlab::host
