// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JTTY_TRANSMIT_TEXT_HPP
#define JTTY_TRANSMIT_TEXT_HPP

#include <utility>

#include <QString>
#include <QVector>

#include "JttyMessages.hpp"
#include "JttyTransmitQueue.hpp"

namespace Jtty
{
  struct EncodedTransmitSegment
  {
    TransmitSegment transmit;       // what the transmit queue sends
    TransmitTextSegment source;     // the prepared characters it carries; offset into the whole text
    bool final {false};             // its last frame ends the message
  };

  struct EncodedTransmitText
  {
    TransmitTextStatus status {TransmitTextStatus::Empty};
    bool substituted {false};       // prepareTransmitText replaced a character
    // In transmit order; on EncodingFailed, those encoded before the failure.
    QVector<EncodedTransmitSegment> segments;
  };

  inline EncodedTransmitText encodeTransmitText (QString const& message,
                                                 NativeExchangeProfile profile, bool isFinal)
  {
    auto const encoded = Encoder::encodeTransmitText (toEncoder (message), profile, isFinal);
    EncodedTransmitText result;
    result.status = encoded.status;
    result.substituted = encoded.substituted;
    for (auto const& from : encoded.segments) {
      EncodedTransmitSegment segment;
      segment.transmit.text = fromEncoder (from.transmit.text);
      for (int const tone : from.transmit.tones) segment.transmit.tones.append (tone);
      for (int const start : from.transmit.frameCharStarts) {
        segment.transmit.frameCharStarts.append (start);
      }
      segment.source = {from.source.offset, from.source.length, fromEncoder (from.source.text)};
      segment.final = from.final;
      result.segments.append (std::move (segment));
    }
    return result;
  }

  struct NativeAtomEncoding
  {
    QVector<int> tones;         // empty when the atoms do not encode
    QString error;
  };

  inline NativeAtomEncoding encodeNativeAtoms (QVector<NativeAtomDescriptor> const& atoms)
  {
    auto const encoded = Encoder::encodeNativeAtoms ({atoms.cbegin (), atoms.cend ()});
    NativeAtomEncoding result;
    for (int const tone : encoded.tones) result.tones.append (tone);
    result.error = fromEncoder (encoded.error);
    return result;
  }

  inline QVector<qint16> renderTransmitTones (int const tones[], int nsym, int sampleRate, float f0)
  {
    auto const audio = Encoder::renderTransmitTones (tones, nsym, sampleRate, f0);
    QVector<qint16> samples;
    samples.reserve (static_cast<int> (audio.size ()));
    for (auto const sample : audio) samples.append (sample);
    return samples;
  }
}

#endif
