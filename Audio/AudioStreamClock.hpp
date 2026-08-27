// -*- Mode: C++ -*-
#ifndef AUDIO_STREAM_CLOCK_HPP__
#define AUDIO_STREAM_CLOCK_HPP__

#include <QtGlobal>

#include "Audio/AudioStreamDescriptor.hpp"

class AudioStreamClock
{
public:
  enum class Reference
  {
    ArrivalWallClock,
    CaptureAnchor
  };

  void setDescriptor (AudioStreamDescriptor descriptor) noexcept
  {
    descriptor_ = descriptor;
    captured_frames_ = 0;
  }

  Reference reference () const noexcept
  {
    return descriptor_.isValid ()
      && descriptor_.clock_domain
         == AudioStreamDescriptor::ClockDomain::SystemClock
      && descriptor_.timing_evidence
         == AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored
      && descriptor_.capture_anchor_utc_ms > 0
      ? Reference::CaptureAnchor : Reference::ArrivalWallClock;
  }

  qint64 timestamp (qint64 arrival_utc_ms) const noexcept
  {
    if (Reference::CaptureAnchor != reference ())
      {
        return arrival_utc_ms;
      }

    return descriptor_.capture_anchor_utc_ms
      + captured_frames_ * 1000 / descriptor_.sample_rate_hz;
  }

  void advance (qint64 frames) noexcept
  {
    if (Reference::CaptureAnchor == reference () && frames > 0)
      {
        captured_frames_ += frames;
      }
  }

private:
  AudioStreamDescriptor descriptor_;
  qint64 captured_frames_ {0};
};

#endif
