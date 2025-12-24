#pragma once

#include <cstdint>
#include <cstddef>

namespace mkvparser {
class MkvReader;
class Segment;
class Cluster;
class BlockEntry;
class Block;
}

class WebmDemuxer {
public:
  WebmDemuxer();
  ~WebmDemuxer();

  bool open(const char* path);
  bool readFrame(uint8_t** data, size_t* size);
  bool seekMs(int64_t timeMs);   // keyframe seek
  void close();

private:
  bool initVideoTrack();
  bool advanceBlock();

  mkvparser::MkvReader* reader_;
  mkvparser::Segment* segment_;
  const mkvparser::Cluster* cluster_;
  const mkvparser::BlockEntry* block_entry_;

  int video_track_;

  uint8_t* buffer_;
  size_t buffer_size_;
};
