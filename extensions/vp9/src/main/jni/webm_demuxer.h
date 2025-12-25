#pragma once

#include <cstdint>
#include <cstddef>

namespace mkvparser {
class MkvReader;
class Segment;
class Cluster;
class BlockEntry;
class Track;
class Block;
}

class WebmDemuxer {
public:
  WebmDemuxer();
  ~WebmDemuxer();

  bool open(const char* path);

  // Reads next *paired* frame (color VP9 + alpha VP9).
  // Buffers are owned by demuxer and valid until next readPairedFrame().
  bool readPairedFrame(const uint8_t** colorData, size_t* colorSize,
                       const uint8_t** alphaData, size_t* alphaSize,
                       int64_t* timeNs);

  bool seekMs(int64_t timeMs);
  void close();

private:
  bool initTracks();
  bool readNextBlockForTrack(const mkvparser::Track* track,
                             const mkvparser::BlockEntry** inOutEntry,
                             const mkvparser::Cluster** inOutCluster,
                             const mkvparser::Block** outBlock);

  static int64_t blockTimeNs(const mkvparser::Block* block,
                            const mkvparser::Cluster* cluster);

  bool readBlockPayload(const mkvparser::Block* block, mkvparser::MkvReader* reader,
                        uint8_t** ioBuf, size_t* ioBufSize,
                        const uint8_t** outPtr, size_t* outSize);

  mkvparser::MkvReader* reader_;
  mkvparser::Segment* segment_;

  const mkvparser::Track* color_track_;
  const mkvparser::Track* alpha_track_;

  const mkvparser::Cluster* color_cluster_;
  const mkvparser::Cluster* alpha_cluster_;
  const mkvparser::BlockEntry* color_entry_;
  const mkvparser::BlockEntry* alpha_entry_;

  // internal buffers for encoded packets
  uint8_t* color_buf_;
  size_t color_buf_size_;
  uint8_t* alpha_buf_;
  size_t alpha_buf_size_;
};