#include "ivf_demuxer.h"
#include <cstdlib>
#include <cstring>

IvfDemuxer::IvfDemuxer()
    : file_(nullptr), buffer_(nullptr), buffer_size_(0) {}

IvfDemuxer::~IvfDemuxer() {
  close();
}

bool IvfDemuxer::open(const char* path) {
  file_ = fopen(path, "rb");
  if (!file_) return false;

  // Skip IVF header (32 bytes)
  fseek(file_, 32, SEEK_SET);
  return true;
}

bool IvfDemuxer::readFrame(uint8_t** data, size_t* size) {
  if (!file_) return false;

  uint32_t frame_size = 0;
  if (fread(&frame_size, 4, 1, file_) != 1) return false;

  // Skip timestamp (8 bytes)
  fseek(file_, 8, SEEK_CUR);

  if (frame_size > buffer_size_) {
    buffer_ = (uint8_t*)realloc(buffer_, frame_size);
    buffer_size_ = frame_size;
  }

  if (fread(buffer_, 1, frame_size, file_) != frame_size) {
    return false;
  }

  *data = buffer_;
  *size = frame_size;
  return true;
}

void IvfDemuxer::close() {
  if (file_) {
    fclose(file_);
    file_ = nullptr;
  }
  if (buffer_) {
    free(buffer_);
    buffer_ = nullptr;
    buffer_size_ = 0;
  }
}
