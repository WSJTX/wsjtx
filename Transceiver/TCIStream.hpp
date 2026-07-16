#ifndef TCI_STREAM_HPP__
#define TCI_STREAM_HPP__

#include <cstddef>
#include <limits>

#include <QByteArray>
#include <QString>
#include <QVersionNumber>
#include <QtGlobal>

namespace TciStream
{
  quint32 constexpr HeaderSize {16u * sizeof (quint32)};
  quint32 constexpr MaxFloatSamples {8192u};
  quint32 constexpr Float32Format {3u};
  quint32 constexpr UncompressedCodec {0u};
  quint32 constexpr DefaultChannels {2u};

  struct Header
  {
    quint32 receiver;
    quint32 sampleRate;
    quint32 format;
    quint32 codec;
    quint32 crc;
    quint32 length;
    quint32 type;
    quint32 channels;
    quint32 reserved[8];
  };

  static_assert (sizeof (Header) == HeaderSize, "TCI stream header must be 64 bytes");
  static_assert (offsetof (Header, length) == 20u, "TCI length field offset changed");
  static_assert (offsetof (Header, type) == 24u, "TCI type field offset changed");
  static_assert (offsetof (Header, channels) == 28u, "TCI channels field offset changed");
  static_assert (sizeof (float) == 4u, "TCI float32 streams require 32-bit float");

  enum StreamType : quint32
  {
    IqStream = 0u,
    RxAudioStream,
    TxAudioStream,
    TxChrono,
  };

  inline bool checked_float_frame_size (quint32 scalar_count, int * size)
  {
    if (!size || scalar_count > MaxFloatSamples)
      {
        return false;
      }

    auto const payload_bytes = static_cast<quint64> (scalar_count) * sizeof (float);
    auto const frame_bytes = static_cast<quint64> (HeaderSize) + payload_bytes;
    if (frame_bytes > static_cast<quint64> ((std::numeric_limits<int>::max) ()))
      {
        return false;
      }

    *size = static_cast<int> (frame_bytes);
    return true;
  }

  inline bool prepare_float_frame (QByteArray * frame, quint32 scalar_count)
  {
    int frame_size;
    if (!frame || !checked_float_frame_size (scalar_count, &frame_size))
      {
        return false;
      }

    frame->resize (frame_size);
    frame->fill ('\0');
    return true;
  }

  inline Header * header (QByteArray * frame)
  {
    return frame && frame->size () >= static_cast<int> (HeaderSize)
      ? reinterpret_cast<Header *> (frame->data ()) : nullptr;
  }

  inline Header const * header (QByteArray const& frame)
  {
    return frame.size () >= static_cast<int> (HeaderSize)
      ? reinterpret_cast<Header const *> (frame.constData ()) : nullptr;
  }

  inline float * float_payload (QByteArray * frame)
  {
    return header (frame)
      ? reinterpret_cast<float *> (frame->data () + HeaderSize) : nullptr;
  }

  inline float const * float_payload (QByteArray const& frame)
  {
    return header (frame)
      ? reinterpret_cast<float const *> (frame.constData () + HeaderSize) : nullptr;
  }

  inline bool has_complete_float_payload (QByteArray const& frame, quint32 scalar_count)
  {
    int expected_size;
    return checked_float_frame_size (scalar_count, &expected_size)
      && frame.size () >= expected_size;
  }

  inline bool protocol_has_channels (QString const& version)
  {
    auto const parsed = QVersionNumber::fromString (version);
    return !parsed.isNull ()
      && QVersionNumber::compare (parsed, QVersionNumber {1, 9}) >= 0;
  }

  inline quint32 channel_count (Header const& stream, bool channel_field_supported)
  {
    if (!channel_field_supported)
      {
        return DefaultChannels;
      }
    return stream.channels == 1u || stream.channels == 2u ? stream.channels : 0u;
  }

  inline bool valid_tx_chrono (Header const& stream, bool channel_field_supported,
                               quint32 expected_sample_rate, quint32 * channels)
  {
    auto const count = channel_count (stream, channel_field_supported);
    if (!channels || !count || stream.format != Float32Format
        || stream.codec != UncompressedCodec || stream.sampleRate != expected_sample_rate
        || stream.length > MaxFloatSamples || stream.length % count)
      {
        return false;
      }

    *channels = count;
    return true;
  }

  inline bool prepare_tx_audio_frame (QByteArray * frame, Header const& chrono,
                                      quint32 channels)
  {
    if ((channels != 1u && channels != 2u) || chrono.length % channels
        || !prepare_float_frame (frame, chrono.length))
      {
        return false;
      }

    auto * response = header (frame);
    response->receiver = chrono.receiver;
    response->sampleRate = chrono.sampleRate;
    response->format = Float32Format;
    response->codec = UncompressedCodec;
    response->length = chrono.length;
    response->type = TxAudioStream;
    response->channels = channels;
    return true;
  }

  inline float * write_channel_frame (float sample, quint32 channels, float * destination)
  {
    for (quint32 channel = 0u; channel < channels; ++channel)
      {
        *destination++ = sample;
      }
    return destination;
  }
}

#endif
