// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JTTY_TRANSMIT_HPP
#define JTTY_TRANSMIT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Jtty
{
  enum class NativeExchangeProfile
  {
    // Shared with the Fortran text packer; None leaves text inference unprofiled.
    None = 0,
    FieldDay = 1,
    RttyRoundup = 2
  };

  // JTTY_ENCODE_* in lib/jtty/jtty_source_codec.f90.
  enum class NativeEncodeStatus
  {
    Ok = 0,
    InvalidDescriptor = 1,
    UnknownSection = 2,
    Unencodable = 3
  };

  // Fixed width of a JTTY transmit frame; genjtty_text_c expects exactly this
  // many characters.
  inline constexpr int maxMessageLength = 80;
  inline constexpr int maxTransmitLength = maxMessageLength;

  // MAX_FRAMES in lib/jtty/jtty_mod.f90, the bits of a frame, and the
  // symbols genjtty_frames sends for each frame.
  inline constexpr int maxTransmitFrames = 16;
  inline constexpr int transmitFrameBits = 34;
  inline constexpr int transmitFrameSymbols = 59;

  enum class TransmitTextStatus
  {
    Encoded,
    Empty,              // nothing but spaces to send
    EncodingFailed
  };
}

extern "C" {
  // lib/jtty/genjtty.f90. genjtty_text_c rewrites msg, a maxTransmitLength-
  // character field, as the text its frames carry; frames receives each
  // frame's transmitFrameBits bits as '0'/'1' characters, frame_starts each
  // frame's 1-based column in msg; status is a NativeEncodeStatus. itone holds
  // maxTransmitFrames * transmitFrameSymbols ints, frames maxTransmitFrames *
  // transmitFrameBits chars, frame_starts maxTransmitFrames ints; all are written.
  void genjtty_text_c (char msg[], int exchange_profile, int is_final, int itone[], int * nsym,
                       char frames[], int * nframes, int frame_starts[], int * status);

  // lib/jtty/gen_jttywave.f90
  void gen_jttywave_ (int const itone[], int * nsym, int * nsps, float * bt, float * fsample,
                      float * f0, float xjunk[], float wave[], int * icmplx, int * nwave);
}

// The JTTY transmit encoding. Text is UTF-16 code units, as in a QString,
// and is handled one unit at a time.
namespace Jtty::Encoder
{
  // QChar::isSpace and QString::trimmed as Qt 5 has them.
  bool isSpace (char16_t c);
  std::u16string trimmed (std::u16string const& text);

  struct PreparedTransmitText
  {
    std::u16string text;
    bool substituted {false};
  };

  PreparedTransmitText prepareTransmitText (std::u16string const& message);

  struct TransmitTextSegment
  {
    int offset {0};
    int length {0};
    std::u16string text;
  };

  // Source spans include separator spaces so every submitted character is accounted for.
  TransmitTextSegment nextTransmitTextSegment (std::u16string const& message, int offset,
                                               int maximumLength = maxTransmitLength);

  // An empty result rejects oversized text before it reaches the fixed-width codec.
  std::u16string transmitFrame (std::u16string const& message);

  // What the transmit queue sends of a segment.
  struct TransmitSegment
  {
    std::u16string text;
    std::vector<int> tones;
    // 0-indexed start offset into text of each encoded frame.
    std::vector<int> frameCharStarts;
  };

  struct EncodedTransmitSegment
  {
    TransmitSegment transmit;
    TransmitTextSegment source;     // the prepared characters it carries; offset into the whole text
    std::vector<std::string> frames;  // the frames behind transmit.tones, transmitFrameBits '0'/'1' each
    bool final {false};             // its last frame ends the message
  };

  struct EncodedTransmitText
  {
    TransmitTextStatus status {TransmitTextStatus::Empty};
    bool substituted {false};       // prepareTransmitText replaced a character
    // In transmit order; on EncodingFailed, those encoded before the failure.
    std::vector<EncodedTransmitSegment> segments;
  };

  // Text as the transmitter sends it. Each line is cut into segments by
  // nextTransmitTextSegment; a segment that does not encode fails the text
  // rather than being cut shorter, since the rest would continue the
  // receiver's 80-column message. The last frame of a line's last segment
  // ends the message unless the line is the text's last and isFinal is
  // false; every other segment leaves it open, so the receiver extends one
  // growing message (jtty_mdecode.f90 stitches a continuation onto it).
  EncodedTransmitText encodeTransmitText (std::u16string const& message,
                                          NativeExchangeProfile profile, bool isFinal);

  // The transmitted audio of tones: gen_jttywave's waveform at sampleRate,
  // lowest tone f0 Hz, scaled to 16 bits, clamped and rounded.
  std::vector<std::int16_t> renderTransmitTones (int const tones[], int nsym, int sampleRate,
                                                 float f0);

}

#endif
