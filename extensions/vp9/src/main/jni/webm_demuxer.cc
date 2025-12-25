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
      color_cap_(0),
      alpha_buf_(nullptr),
      alpha_cap_(0),
      width_(0),
      height_(0) {}

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

  // width/height from color track
  const auto* vt = static_cast<const VideoTrack*>(color_track_);
  width_ = (int)vt->GetWidth();
  height_ = (int)vt->GetHeight();

  return true;
}

void WebmDemuxer::close() {
  if (segment_) {
    delete segment_;
    segment_ = nullptr;
  }
  if (reader_) {
    reader_->Close();
    delete reader_;
    reader_ = nullptr;
  }

  color_track_ = nullptr;
  alpha_track_ = nullptr;
  color_cluster_ = nullptr;
  alpha_cluster_ = nullptr;
  color_entry_ = nullptr;
  alpha_entry_ = nullptr;

  if (color_buf_) { std::free(color_buf_); color_buf_ = nullptr; color_cap_ = 0; }
  if (alpha_buf_) { std::free(alpha_buf_); alpha_buf_ = nullptr; alpha_cap_ = 0; }

  width_ = 0;
  height_ = 0;
}

bool WebmDemuxer::initTracks() {
  const Tracks* tracks = segment_->GetTracks();
  if (!tracks) return false;

  const Track* vp9[2] = {nullptr, nullptr};
  int n = 0;

  for (unsigned i = 0; i < tracks->GetTracksCount(); ++i) {
    const Track* t = tracks->GetTrackByIndex(i);
    if (!t) continue;
    if (t->GetType() != Track::kVideo) continue;
    if (!t->GetCodecId() || std::strcmp(t->GetCodecId(), "V_VP9") != 0) continue;
    if (n < 2) vp9[n++] = t;
  }

  if (n < 2) return false;

  // Prefer name containing "alpha" to decide alpha track.
  auto isAlphaName = [](const char* s) -> bool {
    if (!s) return false;
    for (const char* p = s; *p; ++p) {
      if ((p[0] == 'a' || p[0] == 'A') &&
          (p[1] == 'l' || p[1] == 'L') &&
          (p[2] == 'p' || p[2] == 'P') &&
          (p[3] == 'h' || p[3] == 'H') &&
          (p[4] == 'a' || p[4] == 'A')) return true;
    }
    return false;
  };

  const char* n0 = vp9[0]->GetNameAsUTF8();
  const char* n1 = vp9[1]->GetNameAsUTF8();

  if (isAlphaName(n0) && !isAlphaName(n1)) {
    alpha_track_ = vp9[0];
    color_track_ = vp9[1];
  } else if (isAlphaName(n1) && !isAlphaName(n0)) {
    alpha_track_ = vp9[1];
    color_track_ = vp9[0];
  } else {
    // fallback: first=color, second=alpha
    color_track_ = vp9[0];
    alpha_track_ = vp9[1];
  }

  return color_track_ && alpha_track_;
}

int64_t WebmDemuxer::blockTimeNs(const Block* block, const Cluster* cluster) const {
  if (!block || !cluster) return -1;
  return block->GetTime(cluster);
}

bool WebmDemuxer::nextBlockForTrack(const Track* track,
                                   const Cluster** ioCluster,
                                   const BlockEntry** ioEntry,
                                   const Block** outBlock) {
  if (!track || !ioCluster || !(*ioCluster) || !ioEntry || !outBlock) return false;

  while (true) {
    if (!(*ioEntry)) {
      (*ioCluster)->GetFirst(*ioEntry);
    } else {
      (*ioCluster)->GetNext(*ioEntry, *ioEntry);
    }

    while (!(*ioEntry)) {
      *ioCluster = segment_->GetNext(*ioCluster);
      if (!(*ioCluster)) return false;
      (*ioCluster)->GetFirst(*ioEntry);
    }

    const Block* b = (*ioEntry)->GetBlock();
    if (!b) continue;
    if (b->GetTrackNumber() != track->GetNumber()) continue;

    // Keep simplest: require 1 frame per block.
    if (b->GetFrameCount() != 1) continue;

    *outBlock = b;
    return true;
  }
}

bool WebmDemuxer::readBlockPayload(const Block* block,
                                  uint8_t** ioBuf, size_t* ioBufCap,
                                  const uint8_t** outPtr, size_t* outSize) {
  if (!block || !ioBuf || !ioBufCap || !outPtr || !outSize) return false;
  *outPtr = nullptr;
  *outSize = 0;

  if (block->GetFrameCount() != 1) return false;
  const Block::Frame& f = block->GetFrame(0);
  if (f.len <= 0) return false;

  const size_t need = static_cast<size_t>(f.len);
  if (need > *ioBufCap) {
    uint8_t* nb = static_cast<uint8_t*>(std::realloc(*ioBuf, need));
    if (!nb) return false;
    *ioBuf = nb;
    *ioBufCap = need;
  }

  if (f.Read(reader_, *ioBuf) != 0) return false;

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

  if (!nextBlockForTrack(color_track_, &color_cluster_, &color_entry_, &cb)) return false;
  if (!nextBlockForTrack(alpha_track_, &alpha_cluster_, &alpha_entry_, &ab)) return false;

  int64_t ct = blockTimeNs(cb, color_cluster_);
  int64_t at = blockTimeNs(ab, alpha_cluster_);

  // Align by timestamp by advancing the earlier one.
  while (ct >= 0 && at >= 0 && ct != at) {
    if (ct < at) {
      if (!nextBlockForTrack(color_track_, &color_cluster_, &color_entry_, &cb)) return false;
      ct = blockTimeNs(cb, color_cluster_);
    } else {
      if (!nextBlockForTrack(alpha_track_, &alpha_cluster_, &alpha_entry_, &ab)) return false;
      at = blockTimeNs(ab, alpha_cluster_);
    }
  }

  if (!readBlockPayload(cb, &color_buf_, &color_cap_, colorData, colorSize)) return false;
  if (!readBlockPayload(ab, &alpha_buf_, &alpha_cap_, alphaData, alphaSize)) return false;

  if (timeNs) *timeNs = (ct >= 0) ? ct : at;
  return true;
}

bool WebmDemuxer::seekMs(int64_t /*timeMs*/) {
  // Minimal: not implemented here to keep structure small.
  // If you need seek, we can implement cue-based seek for both tracks.
  return false;
}