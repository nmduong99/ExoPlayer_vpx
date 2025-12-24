#pragma once

#include <cstdint>
#include <cstddef>

namespace mkvparser {
class MkvReader;
class Segment;
class Cluster;
class BlockEntry;
class Track;
}

class WebmDemuxer {
public:
  WebmDemuxer();
  ~WebmDemuxer();

  bool open(const char* path);

  // Returns a pointer to an internal buffer containing exactly one decoded
  // compressed VP9 frame (the encoded frame payload). Valid until next call.
  bool readFrame(uint8_t** data, size_t* size);

  // Keyframe seek using Cues. After a successful seek, the next readFrame()
  // returns from the seeked position (best effort).
  bool seekMs(int64_t timeMs);

  void close();

private:
  bool initVideoTrack();
  bool advanceToNextVp9Block();

  mkvparser::MkvReader* reader_;
  mkvparser::Segment* segment_;
  const mkvparser::Cluster* cluster_;
  const mkvparser::BlockEntry* block_entry_;
  const mkvparser::Track* video_track_;

  uint8_t* buffer_;
  size_t buffer_size_;
};