#pragma once

#include <cstdint>
#include <cstddef>

// Forward declarations (libwebm)
namespace mkvparser {
class MkvReader;
class Segment;
class Cluster;
class BlockEntry;
}

class WebmDemuxer {
public:
  WebmDemuxer();
  ~WebmDemuxer();

  // Open WebM file (VP9 video only)
  bool open(const char* path);

  // Read next VP9 frame (raw bitstream)
  // return false on EOF or error
  bool readFrame(uint8_t** data, size_t* size);

  // Seek to time (milliseconds)
  // Decoder MUST be reset after calling this
  bool seekMs(int64_t timeMs);

  // Close and release resources
  void close();

private:
  bool initTracks();
  bool advanceBlock();

  // libwebm objects
  mkvparser::MkvReader* reader_;
  mkvparser::Segment* segment_;
  const mkvparser::Cluster* cluster_;
  const mkvparser::BlockEntry* block_entry_;

  int video_track_; // VP9 track number

  // Frame buffer
  uint8_t* buffer_;
  size_t buffer_size_;
};
