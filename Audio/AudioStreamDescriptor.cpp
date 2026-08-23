#include "Audio/AudioStreamDescriptor.hpp"

#include <QAudioFormat>

AudioStreamDescriptor audioStreamDescriptorFromQAudioFormat (
  QAudioFormat const& format)
{
  AudioStreamDescriptor descriptor;
  if (!format.isValid () || format.codec () != "audio/pcm"
      || format.sampleSize () <= 0 || format.sampleSize () % 8)
    {
      return descriptor;
    }

  descriptor.sample_rate_hz = format.sampleRate ();
  descriptor.sample_size_bits = format.sampleSize ();
  descriptor.channel_count = format.channelCount ();

  switch (format.sampleType ())
    {
    case QAudioFormat::SignedInt:
      descriptor.sample_encoding =
        AudioStreamDescriptor::SampleEncoding::SignedInteger;
      break;

    case QAudioFormat::UnSignedInt:
      descriptor.sample_encoding =
        AudioStreamDescriptor::SampleEncoding::UnsignedInteger;
      break;

    case QAudioFormat::Float:
      descriptor.sample_encoding =
        AudioStreamDescriptor::SampleEncoding::FloatingPoint;
      break;

    case QAudioFormat::Unknown:
      break;
    }

  if (format.sampleSize () > 8)
    {
      switch (format.byteOrder ())
        {
        case QAudioFormat::LittleEndian:
          descriptor.byte_order = AudioStreamDescriptor::ByteOrder::LittleEndian;
          break;

        case QAudioFormat::BigEndian:
          descriptor.byte_order = AudioStreamDescriptor::ByteOrder::BigEndian;
          break;
        }
    }

  if (1 == descriptor.channel_count)
    {
      descriptor.channel_layout = AudioStreamDescriptor::ChannelLayout::Mono;
    }
  else if (2 == descriptor.channel_count)
    {
      descriptor.channel_layout = AudioStreamDescriptor::ChannelLayout::Stereo;
    }

  return descriptor.isValid () ? descriptor : AudioStreamDescriptor {};
}
