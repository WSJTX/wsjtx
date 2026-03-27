#include "WavFile.hpp"
#include <QAudioFormat>
#include <QDateTime>
#include "Audio/BWFFile.hpp"
#include "revision_utils.hpp"

namespace Radio
{

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
