#include "webm_demuxer.h"

#include <cstdlib>
#include <cstring>

#include "mkvparser/mkvparser.h"
#include "mkvparser/mkvreader.h"

using namespace mkvparser;

WebmDemuxer::WebmDemuxer()
    : reader_(nullptr),
      segment_(nullptr),
      color_track_(nullptr),
      alpha_track_(nullptr),
      color_cluster_(nullptr),
      alpha_cluster_(nullptr),
      color_entry_(nullptr),
      alpha_entry_(nullptr),
      color_buf_(nullptr),
      color_buf_size_(0),
      alpha_buf_(nullptr),
      alpha_buf_size_(0) {}

WebmDemuxer::~WebmDemuxer() { close(); }

bool WebmDemuxer::open(const char* path) {
  close();

  reader_ = new MkvReader();
  if (reader_->Open(path) != 0) {
    close();
    return false;
  }

  long long pos = 0;
  if (Segment::CreateInstance(reader_, pos, segment_) != 0 || !segment_) {
    close();
    return false;
  }

  if (segment_->Load() < 0) {
    close();
    return false;
  }

  if (!initTracks()) {
    close();
    return false;
  }

  color_cluster_ = segment_->GetFirst();
  alpha_cluster_ = segment_->GetFirst();
  color_entry_ = nullptr;
  alpha_entry_ = nullptr;

  return true;
}

bool WebmDemuxer::initTracks() {
  const Tracks* tracks = segment_->GetTracks();

  printf("www webm demuxer get tracks size: %d\n", tracks->GetTracksCount()); 
  if (!tracks) return false;

  // Pick first two VP9 video tracks. Prefer title tags if present.
  const Track* vp9_tracks[2] = {nullptr, nullptr};
  int found = 0;

  for (unsigned i = 0; i < tracks->GetTracksCount(); ++i) {
    const Track* t = tracks->GetTrackByIndex(i);
    if (!t) continue;
    if (t->GetType() != Track::kVideo) continue;
    if (!t->GetCodecId() || std::strcmp(t->GetCodecId(), "V_VP9") != 0) continue;

    if (found < 2) vp9_tracks[found++] = t;
  }

  if (found < 2) {
    // No separate alpha track; caller should use single-track alpha flow instead.
    return false;
  }

  // Heuristic: if track names exist and contain "alpha", use that.
  const Track* t0 = vp9_tracks[0];
  const Track* t1 = vp9_tracks[1];

  const char* n0 = t0->GetNameAsUTF8();
  const char* n1 = t1->GetNameAsUTF8();

  auto isAlphaName = [](const char* s) -> bool {
    if (!s) return false;
    // simple case-insensitive contains "alpha"
    for (const char* p = s; *p; ++p) {
      if ((p[0] == 'a' || p[0] == 'A') &&
          (p[1] == 'l' || p[1] == 'L') &&
          (p[2] == 'p' || p[2] == 'P') &&
          (p[3] == 'h' || p[3] == 'H') &&
          (p[4] == 'a' || p[4] == 'A')) {
        return true;
      }
    }
    return false;
  };

  if (isAlphaName(n0) && !isAlphaName(n1)) {
    alpha_track_ = t0;
    color_track_ = t1;
  } else if (isAlphaName(n1) && !isAlphaName(n0)) {
    alpha_track_ = t1;
    color_track_ = t0;
  } else {
    // fallback: assume first is color, second is alpha
    color_track_ = t0;
    alpha_track_ = t1;
  }

  return color_track_ && alpha_track_;
}

int64_t WebmDemuxer::blockTimeNs(const Block* block, const Cluster* cluster) {
  if (!block || !cluster) return -1;
  return block->GetTime(cluster);
}

bool WebmDemuxer::readNextBlockForTrack(const Track* track,
                                        const BlockEntry** inOutEntry,
                                        const Cluster** inOutCluster,
                                        const Block** outBlock) {
  if (!track || !inOutEntry || !inOutCluster || !outBlock) return false;
  if (!segment_ || !(*inOutCluster)) return false;

  while (true) {
    if (!(*inOutEntry)) {
      (*inOutCluster)->GetFirst(*inOutEntry);
    } else {
      (*inOutCluster)->GetNext(*inOutEntry, *inOutEntry);
    }

    while (!(*inOutEntry)) {
      *inOutCluster = segment_->GetNext(*inOutCluster);
      if (!(*inOutCluster)) return false;
      (*inOutCluster)->GetFirst(*inOutEntry);
    }

    const Block* b = (*inOutEntry)->GetBlock();
    if (!b) continue;
    if (b->GetTrackNumber() != track->GetNumber()) continue;
    if (b->GetFrameCount() != 1) continue;  // keep it simple
    *outBlock = b;
    return true;
  }
}

bool WebmDemuxer::readBlockPayload(const Block* block, MkvReader* reader,
                                  uint8_t** ioBuf, size_t* ioBufSize,
                                  const uint8_t** outPtr, size_t* outSize) {
  if (!block || !reader || !ioBuf || !ioBufSize || !outPtr || !outSize) return false;
  *outPtr = nullptr;
  *outSize = 0;

  if (block->GetFrameCount() != 1) return false;
  const Block::Frame& frame = block->GetFrame(0);
  if (frame.len <= 0) return false;

  const size_t need = static_cast<size_t>(frame.len);
  if (need > *ioBufSize) {
    uint8_t* nb = static_cast<uint8_t*>(std::realloc(*ioBuf, need));
    if (!nb) return false;
    *ioBuf = nb;
    *ioBufSize = need;
  }

  if (frame.Read(reader, *ioBuf) != 0) return false;

  *outPtr = *ioBuf;
  *outSize = need;
  return true;
}

bool WebmDemuxer::readPairedFrame(const uint8_t** colorData, size_t* colorSize,
                                 const uint8_t** alphaData, size_t* alphaSize,
                                 int64_t* timeNs) {
  if (!colorData || !colorSize || !alphaData || !alphaSize) return false;
  *colorData = nullptr; *colorSize = 0;
  *alphaData = nullptr; *alphaSize = 0;
  if (timeNs) *timeNs = -1;

  if (!color_track_ || !alpha_track_) return false;

  const Block* cb = nullptr;
  const Block* ab = nullptr;

  // Prime both.
  if (!readNextBlockForTrack(color_track_, &color_entry_, &color_cluster_, &cb)) return false;
  if (!readNextBlockForTrack(alpha_track_, &alpha_entry_, &alpha_cluster_, &ab)) return false;

  // Align by timestamp (best-effort).
  int64_t ct = blockTimeNs(cb, color_cluster_);
  int64_t at = blockTimeNs(ab, alpha_cluster_);

  // Allow small drift due to encoder, but here we do strict alignment by advancing the earlier.
  while (ct != at) {
    if (ct < 0 || at < 0) break;
    if (ct < at) {
      // advance color
      if (!readNextBlockForTrack(color_track_, &color_entry_, &color_cluster_, &cb)) return false;
      ct = blockTimeNs(cb, color_cluster_);
    } else {
      // advance alpha
      if (!readNextBlockForTrack(alpha_track_, &alpha_entry_, &alpha_cluster_, &ab)) return false;
      at = blockTimeNs(ab, alpha_cluster_);
    }
  }

  if (!readBlockPayload(cb, reader_, &color_buf_, &color_buf_size_, colorData, colorSize)) return false;
  if (!readBlockPayload(ab, reader_, &alpha_buf_, &alpha_buf_size_, alphaData, alphaSize)) return false;

  if (timeNs) *timeNs = (ct >= 0) ? ct : at;
  return true;
}

bool WebmDemuxer::seekMs(int64_t /*timeMs*/) {
  // TODO: implement paired seek using Cues for each track.
  // For now, simplest: return false / not supported.
  return false;
}

void WebmDemuxer::close() {
  if (segment_) { delete segment_; segment_ = nullptr; }
  if (reader_) { reader_->Close(); delete reader_; reader_ = nullptr; }

  color_track_ = nullptr;
  alpha_track_ = nullptr;
  color_cluster_ = nullptr;
  alpha_cluster_ = nullptr;
  color_entry_ = nullptr;
  alpha_entry_ = nullptr;

  if (color_buf_) { std::free(color_buf_); color_buf_ = nullptr; color_buf_size_ = 0; }
  if (alpha_buf_) { std::free(alpha_buf_); alpha_buf_ = nullptr; alpha_buf_size_ = 0; }
}