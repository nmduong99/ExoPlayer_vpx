#include "webm_demuxer.h"

#include <cstdlib>
#include <cstring>

#include "mkvparser/mkvparser.h"
#include "mkvparser/mkvreader.h"

using namespace mkvparser;

WebmDemuxer::WebmDemuxer()
    : reader_(nullptr),
      segment_(nullptr),
      cluster_(nullptr),
      block_entry_(nullptr),
      video_track_(-1),
      buffer_(nullptr),
      buffer_size_(0) {}

WebmDemuxer::~WebmDemuxer() {
  close();
}

// =======================
// OPEN
// =======================
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

  if (!initVideoTrack()) {
    close();
    return false;
  }

  cluster_ = segment_->GetFirst();
  block_entry_ = nullptr;
  return cluster_ != nullptr;
}

// =======================
// FIND VP9 TRACK
// =======================
bool WebmDemuxer::initVideoTrack() {
  const Tracks* tracks = segment_->GetTracks();
  if (!tracks) return false;

  for (unsigned i = 0; i < tracks->GetTracksCount(); ++i) {
    const Track* track = tracks->GetTrackByIndex(i);
    if (!track) continue;

    if (track->GetType() == Track::kVideo &&
        strcmp(track->GetCodecId(), "V_VP9") == 0) {
      video_track_ = track->GetNumber();
      return true;
    }
  }
  return false;
}

// =======================
// READ NEXT FRAME
// =======================
bool WebmDemuxer::readFrame(uint8_t** data, size_t* size) {
  if (!segment_ || !cluster_) return false;

  while (true) {
    if (!block_entry_) {
      cluster_->GetFirst(block_entry_);
    } else {
      cluster_->GetNext(block_entry_, block_entry_);
    }

    while (!block_entry_) {
      cluster_ = segment_->GetNext(cluster_);
      if (!cluster_) return false;
      cluster_->GetFirst(block_entry_);
    }

    const Block* block = block_entry_->GetBlock();
    if (!block) continue;

    if (block->GetTrackNumber() != video_track_) continue;
    if (block->GetFrameCount() <= 0) continue;

    const Block::Frame& frame = block->GetFrame(0);

    if (frame.len <= 0) continue;

    if (frame.len > buffer_size_) {
      buffer_ = static_cast<uint8_t*>(realloc(buffer_, frame.len));
      buffer_size_ = frame.len;
    }

    memcpy(buffer_, frame.data, frame.len);
    *data = buffer_;
    *size = frame.len;
    return true;
  }
}

// =======================
// SEEK (KEYFRAME-BASED)
// =======================
bool WebmDemuxer::seekMs(int64_t timeMs) {
  if (!segment_) return false;

  const Cues* cues = segment_->GetCues();
  if (!cues) return false;

  const int64_t timeNs = timeMs * 1000000LL;

  const CuePoint* cue = nullptr;
  const CuePoint::TrackPosition* track_pos = nullptr;

  if (!cues->Find(timeNs,
                  segment_->GetTracks()->GetTrackByNumber(video_track_),
                  cue,
                  track_pos)) {
    return false;
  }

  if (!track_pos) return false;

  const long long cluster_pos = track_pos->m_pos;

  // Load cluster at position
  if (segment_->LoadCluster(cluster_pos, cluster_) != 0) {
    return false;
  }

  block_entry_ = nullptr;
  return true;
}

// =======================
// CLOSE
// =======================
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

  cluster_ = nullptr;
  block_entry_ = nullptr;
  video_track_ = -1;

  if (buffer_) {
    free(buffer_);
    buffer_ = nullptr;
    buffer_size_ = 0;
  }
}
