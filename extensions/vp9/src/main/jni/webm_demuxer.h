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
  void close();

  // Output pointers are valid until next readPairedFrame.
  bool readPairedFrame(const uint8_t** colorData, size_t* colorSize,
                       const uint8_t** alphaData, size_t* alphaSize,
                       int64_t* timeNs);

  bool seekMs(int64_t timeMs);

  int colorWidth() const { return width_; }
  int colorHeight() const { return height_; }

private:
  bool initTracks();
  bool nextBlockForTrack(const mkvparser::Track* track,
                         const mkvparser::Cluster** ioCluster,
                         const mkvparser::BlockEntry** ioEntry,
                         const mkvparser::Block** outBlock);

  bool readBlockPayload(const mkvparser::Block* block,
                        uint8_t** ioBuf, size_t* ioBufCap,
                        const uint8_t** outPtr, size_t* outSize);

  int64_t blockTimeNs(const mkvparser::Block* block,
                      const mkvparser::Cluster* cluster) const;

  mkvparser::MkvReader* reader_;
  mkvparser::Segment* segment_;

  const mkvparser::Track* color_track_;
  const mkvparser::Track* alpha_track_;

  const mkvparser::Cluster* color_cluster_;
  const mkvparser::Cluster* alpha_cluster_;
  const mkvparser::BlockEntry* color_entry_;
  const mkvparser::BlockEntry* alpha_entry_;

  uint8_t* color_buf_;
  size_t color_cap_;
  uint8_t* alpha_buf_;
  size_t alpha_cap_;

  int width_;
  int height_;
};