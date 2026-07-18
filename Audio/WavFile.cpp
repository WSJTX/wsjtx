#include "WavFile.hpp"
#include <algorithm>
#include <limits>
#include <QAudioFormat>
#include <QDateTime>
#include "Audio/BWFFile.hpp"
#include "revision_utils.hpp"

namespace
{
bool has_native_byte_order (QAudioFormat const& format)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
  return QAudioFormat::LittleEndian == format.byteOrder ();
#else
  return QAudioFormat::BigEndian == format.byteOrder ();
#endif
}

bool is_supported_decode_format (QAudioFormat const& format)
{
  if ("audio/pcm" != format.codec () || 1 != format.channelCount ())
    {
      return false;
    }

  if (11025 == format.sampleRate () && 8 == format.sampleSize ())
    {
      return QAudioFormat::UnSignedInt == format.sampleType ();
    }

  return (11025 == format.sampleRate () || 12000 == format.sampleRate ())
    && 16 == format.sampleSize ()
    && QAudioFormat::SignedInt == format.sampleType ()
    && has_native_byte_order (format);
}
}

namespace Radio
{

WavFile::LoadResult WavFile::load (QString const& name, int max_frames)
{
  LoadResult result;
  if (max_frames <= 0)
    {
      result.error = name + ": invalid WAV frame limit";
      return result;
    }

  BWFFile file {QAudioFormat {}, name};
  if (!file.open (BWFFile::ReadOnly))
    {
      result.error = name + ": " + file.errorString ();
      return result;
    }

  result.format = file.format ();
  auto const bytes_per_frame = result.format.bytesPerFrame ();
  if (!is_supported_decode_format (result.format) || bytes_per_frame <= 0)
    {
      result.error = name + ": unsupported WAV format";
      return result;
    }

  auto max_bytes = std::min<qint64> (
      static_cast<qint64> (max_frames) * bytes_per_frame,
      (std::numeric_limits<int>::max) ());
  max_bytes -= max_bytes % bytes_per_frame;
  auto bytes_to_read = std::min (max_bytes, file.size ());
  bytes_to_read -= bytes_to_read % bytes_per_frame;

  result.samples.resize (static_cast<int> (bytes_to_read));
  qint64 bytes_read {0};
  if (bytes_to_read > 0)
    {
      bytes_read = file.read (result.samples.data (), bytes_to_read);
      if (bytes_read < 0)
        {
          result.samples.clear ();
          result.error = name + ": " + file.errorString ();
          return result;
        }
    }

  bytes_read -= bytes_read % bytes_per_frame;
  result.samples.resize (static_cast<int> (bytes_read));
  result.frames = static_cast<int> (bytes_read / bytes_per_frame);
  return result;
}

QString WavFile::save (QString const& name, short const * data, int samples,
                       QString const& my_callsign, QString const& my_grid,
                       QString const& mode, qint32 sub_mode,
                       Frequency frequency, QString const& his_call,
                       QString const& his_grid, QString const& dgrd)
{
  QAudioFormat format;
  format.setCodec ("audio/pcm");
  format.setSampleRate (12000);
  format.setChannelCount (1);
  format.setSampleSize (16);
  format.setSampleType (QAudioFormat::SignedInt);
  auto source = QString {"%1; %2"}.arg (my_callsign).arg (my_grid);
  auto comment = QString {"Mode=%1%2; Freq=%3%4"}
                   .arg (mode)
                   .arg (QString {(mode.contains ('J') && !mode.contains ('+'))
                         || mode.startsWith ("FST4") || mode.startsWith ('Q')
                         ? QString {"; Sub Mode="} + QString::number (int (samples / 12000)) + QChar {'A' + sub_mode}
                       : QString {}})
                   .arg (Radio::frequency_MHz_string (frequency))
                   .arg (QString {mode!="WSPR" ? QString {"; DXCall=%1; DXGrid=%2"}
         .arg (his_call)
         .arg (his_grid).toLocal8Bit () : ""});
  BWFFile::InfoDictionary list_info {
      {{{'I','S','R','C'}}, source.toLocal8Bit ()},
      {{{'I','S','F','T'}}, program_title (revision ()).simplified ().toLocal8Bit ()},
      {{{'I','C','R','D'}}, QDateTime::currentDateTimeUtc ()
                          .toString ("yyyy-MM-ddTHH:mm:ss.zzzZ").toLocal8Bit ()},
      {{{'I','C','M','T'}}, comment.toLocal8Bit ()},
      {{{'D','G','R','D'}}, dgrd.toLocal8Bit()},  //added for Bob KA1GT
        };
  auto file_name = name + ".wav";
  BWFFile wav {format, file_name, list_info};
  if (!wav.open (BWFFile::WriteOnly)
      || 0 > wav.write (reinterpret_cast<char const *> (data)
                        , sizeof (short) * samples))
    {
      return file_name + ": " + wav.errorString ();
    }
  return QString {};
}

void WavFile::killWaveFile (QString const& name, bool saveAll, bool saveDecoded,
                            bool bDecoded, QString const& mode)
{
  if (name.size () && !(saveAll || (saveDecoded && bDecoded))) {
    QFile f1 {name + ".wav"};
    if (f1.exists ()) f1.remove ();
    if (mode == "WSPR" || mode == "FST4W") {
      QFile f2 {name + ".c2"};
      if (f2.exists ()) f2.remove ();
    }
  }
}

}
