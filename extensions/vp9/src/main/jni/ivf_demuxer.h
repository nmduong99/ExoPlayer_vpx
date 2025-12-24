#pragma once
#include <cstdio>
#include <cstdint>

class IvfDemuxer {
public:
  IvfDemuxer();
  ~IvfDemuxer();

  bool open(const char* path);
  bool readFrame(uint8_t** data, size_t* size);
  void close();

private:
  FILE* file_;
  uint8_t* buffer_;
  size_t buffer_size_;
};
