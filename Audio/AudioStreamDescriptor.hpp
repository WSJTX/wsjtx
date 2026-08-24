// -*- Mode: C++ -*-
#ifndef AUDIO_STREAM_DESCRIPTOR_HPP__
#define AUDIO_STREAM_DESCRIPTOR_HPP__

#include <QMetaType>

class QAudioFormat;

struct AudioStreamDescriptor
{
  enum class SampleEncoding
  {
    Unknown,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint
  };

  enum class ByteOrder
  {
    Unknown,
    LittleEndian,
    BigEndian
  };

  enum class ChannelLayout
  {
    Unknown,
    Mono,
    Stereo
  };

  enum class ClockDomain
  {
    Unknown,
    DeviceClock,
    SystemClock
  };

  enum class TimingEvidence
  {
    Unavailable,
    PositionCountable,
    CaptureTimeAnchored
  };

  int sample_rate_hz {0};
  SampleEncoding sample_encoding {SampleEncoding::Unknown};
  int sample_size_bits {0};
  ByteOrder byte_order {ByteOrder::Unknown};
  int channel_count {0};
  ChannelLayout channel_layout {ChannelLayout::Unknown};
  ClockDomain clock_domain {ClockDomain::Unknown};
  TimingEvidence timing_evidence {TimingEvidence::Unavailable};

  // True only when the source has a mechanism for reporting discontinuities.
  bool can_report_discontinuities {false};

  bool isValid () const noexcept
  {
    return sample_rate_hz > 0
      && SampleEncoding::Unknown != sample_encoding
      && sample_size_bits > 0
      && (sample_size_bits <= 8 || ByteOrder::Unknown != byte_order)
      && channel_count > 0;
  }
};

inline bool operator== (AudioStreamDescriptor const& lhs,
                        AudioStreamDescriptor const& rhs) noexcept
{
  return lhs.sample_rate_hz == rhs.sample_rate_hz
    && lhs.sample_encoding == rhs.sample_encoding
    && lhs.sample_size_bits == rhs.sample_size_bits
    && lhs.byte_order == rhs.byte_order
    && lhs.channel_count == rhs.channel_count
    && lhs.channel_layout == rhs.channel_layout
    && lhs.clock_domain == rhs.clock_domain
    && lhs.timing_evidence == rhs.timing_evidence
    && lhs.can_report_discontinuities == rhs.can_report_discontinuities;
}

inline bool operator!= (AudioStreamDescriptor const& lhs,
                        AudioStreamDescriptor const& rhs) noexcept
{
  return !(lhs == rhs);
}

AudioStreamDescriptor audioStreamDescriptorFromQAudioFormat (
  QAudioFormat const& format);

Q_DECLARE_METATYPE (AudioStreamDescriptor)

#endif
