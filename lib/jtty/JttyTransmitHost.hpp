// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JTTY_TRANSMIT_HOST_HPP
#define JTTY_TRANSMIT_HOST_HPP

#include <cstdint>

namespace Jtty::Encoder
{
  enum class TransmitStatus : std::int32_t
  {
    Encoded = 0,
    Empty = 1,           // nothing but spaces to send
    EncodingFailed = 2,
    InvalidRuntime = 3,  // a native template its context cannot fill
    TooLong = 4,
    BadRequest = 5,
    NotConfigured = 6,   // it reads the station's call or grid, and no usable one is set
    Missing = 7          // it reads serial or report, and the request has none
  };

  // In UTF-16 units: text and template; text once expanded; his call and
  // exchange. n1mm takes as many characters as WSJT-X takes bytes in an
  // MMTTY command (widgets/MMTTYIF.cpp).
  inline constexpr int maxRequestText = 32767;
  inline constexpr int maxN1mmText = 64 * 1024;
  inline constexpr int maxExpandedText = 1 << 20;
  inline constexpr int maxContextText = 64;
}

// The transmit encoding for lib/streaming_jtty.f90: an encode request encoded
// as the GUI sends it, held by handle. Strings are UTF-8 with explicit
// lengths, and every pointer is required. No entry lets a C++ exception
// escape.
extern "C" {
  // 0 when no handle is free or the encoder failed. my_call and grid are the
  // station's from configure, their length -1 when none has set them and -2
  // when the configured value is longer than maxContextText; serial and
  // report count only when their *_given is nonzero. open_columns and
  // open_separator (0 or 1) are the receiver's message the request
  // continues, both 0 for none (lib/streaming_jtty_width.f90); the request
  // keeps every message within the receiver's 80 columns. kind says what
  // text is: 0 typed text, 1 a template, 2 a TXTEXT from N1MM Logger+.
  std::int32_t jtty_tx_encode (std::int32_t kind, char const * text, std::int32_t text_length,
                               char const * his_call, std::int32_t his_call_length,
                               char const * exchange, std::int32_t exchange_length,
                               char const * my_call, std::int32_t my_call_length,
                               char const * grid, std::int32_t grid_length,
                               std::int32_t serial, std::int32_t serial_given,
                               std::int32_t report, std::int32_t report_given,
                               std::int32_t profile, std::int32_t is_final,
                               std::int32_t open_columns, std::int32_t open_separator) noexcept;
  // The receiver's message after an encoded request: both 0 when its last
  // segment ends it, else the columns it holds and whether its last frame
  // owes a separator.
  void jtty_tx_open (std::int32_t handle, std::int32_t * columns, std::int32_t * separator) noexcept;
  // A TransmitStatus; substituted is any replacement in the request's text.
  std::int32_t jtty_tx_status (std::int32_t handle, std::int32_t * segments, std::int32_t * substituted) noexcept;
  // Why the request is not sent: the request key at fault (possibly none) and
  // a description, each truncated to its capacity.
  void jtty_tx_error (std::int32_t handle, char * key, std::int32_t key_capacity, std::int32_t * key_length,
                      char * detail, std::int32_t detail_capacity, std::int32_t * detail_length) noexcept;
  // Segment index (0-based) in transmit order: tones holds maxTransmitFrames *
  // transmitFrameSymbols, frames maxTransmitFrames * transmitFrameBits, text
  // and canonical maxTransmitLength each. text is the characters it carries,
  // trimmed; canonical what its frames carry; substituted whether a character
  // of it was replaced. Returns its tone count, or 0 when there is no such
  // segment.
  std::int32_t jtty_tx_segment (std::int32_t handle, std::int32_t index, std::int32_t * tones,
                                char * frames, std::int32_t * nframes,
                                char * text, std::int32_t * text_length,
                                char * canonical, std::int32_t * canonical_length,
                                std::int32_t * is_final, std::int32_t * substituted) noexcept;
  void jtty_tx_destroy (std::int32_t handle) noexcept;
  // renderTransmitTones into samples; returns the sample count, or 0 when it
  // exceeds capacity or capacity is negative.
  std::int32_t jtty_tx_render (std::int32_t const * tones, std::int32_t nsym, std::int32_t sample_rate,
                               float f0, std::int16_t * samples, std::int32_t capacity) noexcept;
}

#endif
