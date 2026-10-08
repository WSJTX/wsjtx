// SPDX-License-Identifier: GPL-3.0-or-later
#include "JttyTransmit.hpp"

#include <algorithm>

namespace
{
  std::u16string fromLatin1 (char const * text, std::size_t length)
  {
    std::u16string result;
    result.reserve (length);
    for (std::size_t i = 0; i < length; ++i) result += static_cast<unsigned char> (text[i]);
    return result;
  }

  // As QString::toLatin1: a unit above U+00FF becomes '?'.
  std::string toLatin1 (std::u16string const& text)
  {
    std::string result;
    result.reserve (text.size ());
    for (char16_t const c : text) result += c > 0xFF ? '?' : static_cast<char> (c);
    return result;
  }

  // Each of n frames as a string of Jtty::transmitFrameBits '0'/'1' characters.
  std::vector<std::string> frameStrings (char const bits[], int n)
  {
    std::vector<std::string> frames;
    for (int i = 0; i < n; ++i) {
      frames.emplace_back (bits + i * Jtty::transmitFrameBits, Jtty::transmitFrameBits);
    }
    return frames;
  }

  // As Qt 5's qRound (float), which differs from std::lround near halves.
  int qtRound (float d)
  {
    return d >= 0.0f ? int (d + 0.5f) : int (d - float (int (d - 1)) + 0.5f) + int (d - 1);
  }
}

namespace Jtty::Encoder
{
  namespace
  {
    std::u16string sourceAlphabet ()
    {
      // Keep in sync with ALPHABET in lib/jtty/jtty_source_codec.f90.
      return u"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!\"#$%,&*()_'=[]{}<>|:;";
    }

    bool isSourceCharacter (char16_t c)
    {
      return sourceAlphabet ().find (c) != std::u16string::npos || (c >= u'a' && c <= u'z');
    }
  }

  PreparedTransmitText prepareTransmitText (std::u16string const& message)
  {
    PreparedTransmitText result;
    result.text.reserve (message.size ());

    for (char16_t c : message) {
      if (c == u'\0' || c == u'~') {
        result.text += u' ';
        result.substituted = true;
      } else if (c == u'\n' || c == u'\r') {
        // A forced segment boundary, not a character; encodeTransmitText splits on it.
        result.text += u'\n';
      } else if (isSourceCharacter (c)) {
        result.text += c;
      } else {
        result.text += u'#';
        result.substituted = true;
      }
    }

    return result;
  }

  TransmitTextSegment nextTransmitTextSegment (std::u16string const& message, int offset,
                                               int maximumLength)
  {
    int const size = static_cast<int> (message.size ());
    if (offset < 0 || offset >= size || maximumLength <= 0) return {};

    int length = std::min ({size - offset, maximumLength, maxTransmitLength});
    if (offset + length < size && message[offset + length] != u' ') {
      for (int i = length - 1; i > 0; --i) {
        if (message[offset + i] == u' ' && !trimmed (message.substr (offset, i)).empty ()) {
          length = i + 1;
          break;
        }
      }
    }
    return {offset, length, message.substr (offset, length)};
  }

  std::u16string transmitFrame (std::u16string const& message)
  {
    if (message.size () > static_cast<std::size_t> (maxTransmitLength)) return {};
    return message + std::u16string (maxTransmitLength - message.size (), u' ');
  }

  EncodedTransmitText encodeTransmitText (std::u16string const& message,
                                          NativeExchangeProfile profile, bool isFinal)
  {
    EncodedTransmitText result;
    auto const prepared = prepareTransmitText (message);
    if (trimmed (prepared.text).empty ()) return result;
    result.substituted = prepared.substituted;

    int const exchangeProfile = static_cast<int> (profile);
    // Fills segment's transmit and frames; false when the text does not encode.
    auto encodeOnce = [exchangeProfile] (std::u16string const& text, bool segmentIsFinal,
                                         EncodedTransmitSegment& segment) {
      auto frame = toLatin1 (transmitFrame (text));
      if (frame.size () != static_cast<std::size_t> (maxTransmitLength)) return false;
      std::vector<int> tones (maxTransmitFrames * transmitFrameSymbols);
      char bits[maxTransmitFrames * transmitFrameBits];
      int frameStarts[maxTransmitFrames] = {};
      int nsym = 0;
      int nframes = 0;
      int status = -1;
      genjtty_text_c (&frame[0], exchangeProfile, segmentIsFinal ? 1 : 0, tones.data (), &nsym,
                      bits, &nframes, frameStarts, &status);
      if (status != static_cast<int> (NativeEncodeStatus::Ok) || nsym <= 0) return false;
      segment.transmit = TransmitSegment {};
      segment.transmit.tones.assign (tones.begin (), tones.begin () + nsym);
      segment.transmit.text = trimmed (fromLatin1 (frame.data (), frame.size ()));
      for (int i = 0; i < nframes; ++i) {
        segment.transmit.frameCharStarts.push_back (frameStarts[i] - 1);
      }
      segment.frames = frameStrings (bits, nframes);
      return true;
    };

    std::vector<std::u16string> lines;
    for (std::size_t start = 0;;) {
      auto const end = prepared.text.find (u'\n', start);
      lines.push_back (prepared.text.substr (start, end - start));
      if (end == std::u16string::npos) break;
      start = end + 1;
    }
    int lineOffset = 0;
    for (std::size_t lineIndex = 0; lineIndex < lines.size (); ++lineIndex) {
      auto const& line = lines[lineIndex];
      int const lineSize = static_cast<int> (line.size ());
      auto const lineStart = result.segments.size ();
      for (int offset = 0; offset < lineSize;) {
        auto const source = nextTransmitTextSegment (line, offset);
        if (trimmed (source.text).empty ()) {
          offset += source.length;
          continue;
        }
        EncodedTransmitSegment segment;
        if (!encodeOnce (source.text, /*segmentIsFinal=*/false, segment)) {
          result.status = TransmitTextStatus::EncodingFailed;
          return result;
        }
        segment.source = {lineOffset + source.offset, source.length, source.text};
        result.segments.push_back (std::move (segment));
        offset += source.length;
      }
      lineOffset += lineSize + 1;
      if (result.segments.size () == lineStart) continue;
      bool const lineIsFinal = lineIndex + 1 < lines.size () || isFinal;
      if (!lineIsFinal) continue;
      auto& last = result.segments.back ();
      if (!encodeOnce (last.transmit.text, /*segmentIsFinal=*/true, last)) {
        result.status = TransmitTextStatus::EncodingFailed;
        return result;
      }
      last.final = true;
    }
    result.status = TransmitTextStatus::Encoded;
    return result;
  }

  std::vector<std::int16_t> renderTransmitTones (int const tones[], int nsym, int sampleRate,
                                                 float f0)
  {
    int nsps = 384 * sampleRate / 12000;
    float bt = 2.0;
    float fsample = float (sampleRate);
    int icmplx = 0;
    int nwave = nsps * nsym;

    std::vector<float> wave (nwave > 0 ? nwave : 1);
    gen_jttywave_ (tones, &nsym, &nsps, &bt, &fsample, &f0,
                   wave.data (), wave.data (), &icmplx, &nwave);

    std::vector<std::int16_t> samples;
    samples.reserve (nwave > 0 ? nwave : 0);
    for (int i = 0; i < nwave; ++i) {
      float v = wave[i] * 32767.0f;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32768.0f) v = -32768.0f;
      samples.push_back (static_cast<std::int16_t> (qtRound (v)));
    }
    return samples;
  }
}
