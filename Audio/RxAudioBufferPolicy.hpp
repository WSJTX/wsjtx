// -*- Mode: C++ -*-
#ifndef RX_AUDIO_BUFFER_POLICY_HPP_
#define RX_AUDIO_BUFFER_POLICY_HPP_

// Chooses the soundcard capture buffer size (in input frames) passed to
// AudioInputSource::start ().
//
// Full Duplex keeps the capture stream running while transmitting. When a
// transmission ends, stopping the output stream runs on the same audio
// thread that services the capture stream, and with the buffer size Qt
// chooses by default the capture buffer can overflow, dropping tens of
// milliseconds of input. Satellite and other loopback users hear their own
// signal after a path delay, so the loss lands in the last frame of their
// own returning transmission (e.g. a JTTY end-of-message frame). A larger
// capture buffer rides through the stall.
//
// The larger buffer is applied only in Full Duplex because it also makes
// Qt deliver input in bigger chunks, which coarsens the arrival-clock
// timing used by the period-based modes.

namespace RxAudioBufferPolicy
{
  // Let Qt choose the buffer size.
  constexpr int qt_default_frames {-1};

  // About one second at the 48 kHz capture rate. Measured over QO-100
  // loopback: no end-of-transmission loss with this size, 30-60 ms loss
  // without it.
  constexpr int full_duplex_frames {48000};

  // env_override_frames is WSJT_RX_AUDIO_BUFFER_FRAMES (0 when unset or
  // invalid); a non-zero override always wins so it remains usable for
  // diagnostics in either mode.
  constexpr int select (int env_override_frames, bool full_duplex) noexcept
  {
    return env_override_frames
      ? env_override_frames
      : (full_duplex ? full_duplex_frames : qt_default_frames);
  }
}

#endif
