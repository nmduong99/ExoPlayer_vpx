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
      video_track_(nullptr),
      buffer_(nullptr),
      buffer_size_(0) {}

WebmDemuxer::~WebmDemuxer() {
  close();
}

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

  // Load headers + clusters. For large files this is heavy, but matches your
  // current design. If you want true streaming, we can switch to ParseHeaders()
  // + incremental LoadCluster().
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

  // Position at first VP9 block.
  if (!advanceToNextVp9Block()) {
    // Could be empty or non-vp9-only file.
    return false;
  }

  return true;
}

bool WebmDemuxer::initVideoTrack() {
  const Tracks* tracks = segment_->GetTracks();
  if (!tracks) return false;

  for (unsigned i = 0; i < tracks->GetTracksCount(); ++i) {
    const Track* track = tracks->GetTrackByIndex(i);
    if (!track) continue;

    if (track->GetType() == Track::kVideo &&
        track->GetCodecId() &&
        std::strcmp(track->GetCodecId(), "V_VP9") == 0) {
      video_track_ = track;
      return true;
    }
  }
  return false;
}

bool WebmDemuxer::advanceToNextVp9Block() {
  if (!segment_ || !cluster_ || !video_track_) return false;

  while (true) {
    // Step within cluster.
    if (!block_entry_) {
      cluster_->GetFirst(block_entry_);
    } else {
      cluster_->GetNext(block_entry_, block_entry_);
    }

    // Move to next cluster if needed.
    while (!block_entry_) {
      cluster_ = segment_->GetNext(cluster_);
      if (!cluster_) return false;
      cluster_->GetFirst(block_entry_);
    }

    const Block* block = block_entry_->GetBlock();
    if (!block) continue;

    if (block->GetTrackNumber() != video_track_->GetNumber()) continue;

    const int frame_count = block->GetFrameCount();
    if (frame_count <= 0) continue;

    // Most VP9-in-WebM is one frame per block. If laced, simplest is to skip
    // (or we can concatenate; ask if you want that).
    if (frame_count != 1) {
      continue;
    }

    const Block::Frame& frame = block->GetFrame(0);
    if (frame.len <= 0) continue;

    return true;
  }
}

bool WebmDemuxer::readFrame(uint8_t** data, size_t* size) {
  if (!data || !size) return false;
  *data = nullptr;
  *size = 0;

  if (!segment_ || !cluster_ || !block_entry_ || !video_track_) return false;

  const Block* block = block_entry_->GetBlock();
  if (!block) return false;

  // Ensure we’re on a valid VP9 block. If not, advance.
  if (block->GetTrackNumber() != video_track_->GetNumber() ||
      block->GetFrameCount() != 1) {
    if (!advanceToNextVp9Block()) return false;
    block = block_entry_->GetBlock();
    if (!block) return false;
  }

  const Block::Frame& frame = block->GetFrame(0);

  if (static_cast<size_t>(frame.len) > buffer_size_) {
    uint8_t* new_buf = static_cast<uint8_t*>(std::realloc(buffer_, frame.len));
    if (!new_buf) return false;
    buffer_ = new_buf;
    buffer_size_ = static_cast<size_t>(frame.len);
  }

  // IMPORTANT: mkvparser::Block::Frame has no "data". Use Frame::Read().
  const long read_status = frame.Read(reader_, buffer_);
  if (read_status != 0) {
    return false;
  }

  *data = buffer_;
  *size = static_cast<size_t>(frame.len);

  // Advance for next call.
  if (!advanceToNextVp9Block()) {
    // EOF is fine: caller will get false next time.
  }

  return true;
}

bool WebmDemuxer::seekMs(int64_t timeMs) {
  if (!segment_ || !video_track_) return false;

  const Cues* cues = segment_->GetCues();
  if (!cues) return false;

  const long long timeNs = static_cast<long long>(timeMs) * 1000000LL;

  const CuePoint* cue = nullptr;
  const CuePoint::TrackPosition* track_pos = nullptr;

  if (!cues->Find(timeNs, video_track_, cue, track_pos)) {
    return false;
  }
  if (!cue || !track_pos) return false;

  // track_pos->m_pos is cluster position (relative to segment start)
  const Cluster* c = segment_->FindOrPreloadCluster(track_pos->m_pos);
  if (!c || c->EOS()) return false;

  // Try to get the exact block from cue information.
  const BlockEntry* be = c->GetEntry(*cue, *track_pos);
  if (!be || be->EOS()) {
    // Fallback: start from first block in cluster, then advance until we find VP9.
    cluster_ = c;
    block_entry_ = nullptr;
    return advanceToNextVp9Block();
  }

  cluster_ = c;
  block_entry_ = be;

  // Ensure next readFrame() returns a VP9 block.
  const Block* block = block_entry_->GetBlock();
  if (!block || block->GetTrackNumber() != video_track_->GetNumber()) {
    return advanceToNextVp9Block();
  }

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

  cluster_ = nullptr;
  block_entry_ = nullptr;
  video_track_ = nullptr;

  if (buffer_) {
    std::free(buffer_);
    buffer_ = nullptr;
    buffer_size_ = 0;
  }
}