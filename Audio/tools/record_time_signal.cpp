#include <iostream>
#include <exception>
#include <stdexcept>
#include <string>
#include <memory>
#include <locale>

#include <QCoreApplication>
#include <QTextStream>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QStringList>
#include <QFileInfo>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#include <QTimer>
#include <QDateTime>
#include <QDebug>

#include "revision_utils.hpp"
#include "Audio/BWFFile.hpp"

namespace
{
  QTextStream qtout {stdout};
}

class Record final
  : public QObject
{
  Q_OBJECT;

public:
  Record (int start, int duration, QAudioDeviceInfo const& source_device, BWFFile * output, int notify_interval, int buffer_size)
    : source_ {source_device, output->format ()}
    , notify_interval_ {notify_interval}
    , output_ {output}
    , duration_ {duration}
  {
    if (buffer_size) source_.setBufferSize (output_->format ().bytesForFrames (buffer_size));
    if (notify_interval_)
      {
        source_.setNotifyInterval (notify_interval);
        connect (&source_, &QAudioInput::notify, this, &Record::notify);
      }

    if (start == -1)
      {
        start_recording ();
      }
    else
      {
        auto now = QDateTime::currentDateTimeUtc ();
        auto time = now.time ();
        auto then = now;
        then.setTime (QTime {time.hour (), time.minute (), start});
        auto delta_ms = (now.msecsTo (then) + (60 * 1000)) % (60 * 1000);
        QTimer::singleShot (int (delta_ms), Qt::PreciseTimer, this, &Record::start_recording);
      }
  }

  Q_SIGNAL void done ();

private:
  Q_SLOT void start_recording ()
  {
    qtout << "started recording at " << QDateTime::currentDateTimeUtc ().toString ("hh:mm:ss.zzz UTC")
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
          << Qt::endl
#else
          << endl
#endif
      ;
    source_.start (output_);
    if (!notify_interval_) QTimer::singleShot (duration_ * 1000, Qt::PreciseTimer, this, &Record::stop_recording);
    qtout << QString {"buffer size used is: %1"}.arg (source_.bufferSize ())
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                                                   << Qt::endl
#else
                                                   << endl
#endif
                                                   ;
  }

  Q_SLOT void notify ()
  {
    auto length = source_.elapsedUSecs ();
    qtout << QString {"%1 μs recorded\r"}.arg (length)
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                                             << Qt::flush
#else
                                             << flush
#endif
                                             ;
    if (length >= duration_ * 1000 * 1000) stop_recording ();
  }

  Q_SLOT void stop_recording ()
  {
    auto length = source_.elapsedUSecs ();
    source_.stop ();
    qtout << QString {"%1 μs recorded "}.arg (length) << '(' << source_.format ().framesForBytes (output_->size ()) << " frames recorded)\n";
    qtout << "stopped recording at " << QDateTime::currentDateTimeUtc ().toString ("hh:mm:ss.zzz UTC")
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
          << Qt::endl
#else
          << endl
#endif
      ;
    Q_EMIT done ();
  }

  QAudioInput source_;
  int notify_interval_;
  BWFFile * output_;
  int duration_;
};

class Playback final
  : public QObject
{
  Q_OBJECT;

public:
  Playback (int start, BWFFile * input, QAudioDeviceInfo const& sink_device, int notify_interval, int buffer_size, QString const& category)
    : input_ {input}
    , sink_ {sink_device, input->format ()}
    , notify_interval_ {notify_interval}
  {
    if (buffer_size) sink_.setBufferSize (input_->format ().bytesForFrames (buffer_size));
    if (category.size ()) sink_.setCategory (category);
    if (notify_interval_)
      {
        sink_.setNotifyInterval (notify_interval);
        connect (&sink_, &QAudioOutput::notify, this, &Playback::notify);
      }
    connect (&sink_, &QAudioOutput::stateChanged, this, &Playback::sink_state_changed);
    if (start == -1)
      {
        start_playback ();
      }
    else
      {
        auto now = QDateTime::currentDateTimeUtc ();
        auto time = now.time ();
        auto then = now;
        then.setTime (QTime {time.hour (), time.minute (), start});
        auto delta_ms = (now.msecsTo (then) + (60 * 1000)) % (60 * 1000);
        QTimer::singleShot (int (delta_ms), Qt::PreciseTimer, this, &Playback::start_playback);
      }
  }
  
  Q_SIGNAL void done ();

private:
  Q_SLOT void start_playback ()
  {
    qtout << "started playback at " << QDateTime::currentDateTimeUtc ().toString ("hh:mm:ss.zzz UTC")
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
          << Qt::endl
#else
          << endl
#endif
      ;
    sink_.start (input_);
    qtout << QString {"buffer size used is: %1 (%2 frames)"}.arg (sink_.bufferSize ()).arg (sink_.format ().framesForBytes (sink_.bufferSize ()))
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                                                               << Qt::endl
#else
                                                               << endl
#endif
                                                               ;
  }

  Q_SLOT void notify ()
  {
    auto length = sink_.elapsedUSecs ();
    qtout << QString {"%1 μs rendered\r"}.arg (length) <<
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                                             Qt::flush
#else
                                             flush
#endif
                                             ;
  }

  Q_SLOT void sink_state_changed (QAudio::State state)
  {
    switch (state)
      {
      case QAudio::ActiveState:
        qtout << "\naudio output state changed to active\n";
        break;
      case QAudio::SuspendedState:
        qtout << "\naudio output state changed to suspended\n";
        break;
      case QAudio::StoppedState:
        qtout << "\naudio output state changed to stopped\n";
        break;
      case QAudio::IdleState:
        stop_playback ();
        qtout << "\naudio output state changed to idle\n";
        break;
#if QT_VERSION >= QT_VERSION_CHECK (5, 10, 0)
      case QAudio::InterruptedState:
        qtout << "\naudio output state changed to interrupted\n";
        break;
#endif
      }
  }

  Q_SLOT void stop_playback ()
  {
    auto length = sink_.elapsedUSecs ();
    sink_.stop ();
    qtout << QString {"%1 μs rendered "}.arg (length) << '(' << sink_.format ().framesForBytes (input_->size ()) << " frames rendered)\n";
    qtout << "stopped playback at " << QDateTime::currentDateTimeUtc ().toString ("hh:mm:ss.zzz UTC")
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
          << Qt::endl
#else
          << endl
#endif
      ;
    Q_EMIT done ();
  }

  BWFFile * input_;
  QAudioOutput sink_;
  int notify_interval_;
};

#include "record_time_signal.moc"

int main(int argc, char *argv[])
{
  QCoreApplication app {argc, argv};
  try
    {
      // ensure number forms are in consistent format, do this after
      // instantiating QApplication so that Qt has correct l18n
      std::locale::global (std::locale::classic ());

      // Override programs executable basename as application name.
      app.setApplicationName ("WSJT-X Record Time Signal");
      app.setApplicationVersion (version ());

      QCommandLineParser parser;
      parser.setApplicationDescription (
                                        "\nTool to determine and experiment with QAudioInput latencies\n\n"
                                        "\tUse the -I option to list available recording device numbers\n"
                                        );
      auto help_option = parser.addHelpOption ();
      auto version_option = parser.addVersionOption ();

      parser.addOptions ({
          {{"I", "list-audio-inputs"},
              app.translate ("main", "List the available audio input devices")},
          {{"O", "list-audio-outputs"},
              app.translate ("main", "List the available audio output devices")},
          {{"s", "start-time"},
              app.translate ("main", "Record from <start-time> seconds, default start immediately"),
              app.translate ("main", "start-time")},
          {{"d", "duration"},
              app.translate ("main", "Recording <duration> seconds"),
              app.translate ("main", "duration")},
          {{"o", "output"},
              app.translate ("main", "Save output as <output-file>"),
              app.translate ("main", "output-file")},
          {{"i", "input"},
              app.translate ("main", "Playback <input-file>"),
              app.translate ("main", "input-file")},
          {{"f", "force"},
              app.translate ("main", "Overwrite existing file")},
          {{"r", "sample-rate"},
              app.translate ("main", "Record at <sample-rate>, default 48000 Hz"),
              app.translate ("main", "sample-rate")},
          {{"c", "num-channels"},
              app.translate ("main", "Record <num> channels, default 2"),
              app.translate ("main", "num")},
          {{"R", "recording-device-number"},
              app.translate ("main", "Record from <device-number>"),
              app.translate ("main", "device-number")},
          {{"P", "playback-device-number"},
              app.translate ("main", "Playback to <device-number>"),
              app.translate ("main", "device-number")},
          {{"C", "category"},
              app.translate ("main", "Playback <category-name>"),
              app.translate ("main", "category-name")},
          {{"n", "notify-interval"},
              app.translate ("main", "use notify signals every <interval> milliseconds, zero to use a timer"),
              app.translate ("main", "interval")},
          {{"b", "buffer-size"},
              app.translate ("main", "audio buffer size <frames>"),
              app.translate ("main", "frames")},
        });
      parser.process (app);

      auto input_devices = QAudioDeviceInfo::availableDevices (QAudio::AudioInput);      
      if (parser.isSet ("I"))
        {
          int n {0};
          for (auto const& device : input_devices)
            {
              qtout << ++n << " - [" << device.deviceName () << ']'
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
                    << Qt::endl
#else
                    << endl
#endif
                ;
            }
          return 0;
        }

      auto output_devices = QAudioDeviceInfo::availableDevices (QAudio::AudioOutput);      
      if (parser.isSet ("O"))
        {
          int n {0};
          for (auto const& device : output_devices)
            {
              qtout << ++n << " - [" << device.deviceName () << ']'
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
                    << Qt::endl
#else
                    << endl
#endif
                ;
            }
          return 0;
        }

      bool ok;
      int start {-1};
      if (parser.isSet ("s"))
        {
          start = parser.value ("s").toInt (&ok);
          if (!ok) throw std::invalid_argument {"start time not a number"};
          if (0 > start || start > 59) throw std::invalid_argument {"0 > start > 59"};
        }
      int sample_rate {48000};
      if (parser.isSet ("r"))
        {
          sample_rate = parser.value ("r").toInt (&ok);
          if (!ok) throw std::invalid_argument {"sample rate not a number"};
        }
      int num_channels {2};
      if (parser.isSet ("c"))
        {
          num_channels = parser.value ("c").toInt (&ok);
          if (!ok) throw std::invalid_argument {"channel count not a number"};
        }
      int notify_interval {0};
      if (parser.isSet ("n"))
        {
          notify_interval = parser.value ("n").toInt (&ok);
          if (!ok) throw std::invalid_argument {"notify interval not a number"};
        }
      int buffer_size {0};
      if (parser.isSet ("b"))
        {
          buffer_size = parser.value ("b").toInt (&ok);
          if (!ok) throw std::invalid_argument {"buffer size not a number"};
        }
      int input_device {0};
      if (parser.isSet ("R"))
        {
          input_device = parser.value ("R").toInt (&ok);
          if (!ok || 0 >= input_device || input_device > input_devices.size ())
            {
              throw std::invalid_argument {"invalid recording device"};
            }
        }
      int output_device {0};
      if (parser.isSet ("P"))
        {
          output_device = parser.value ("P").toInt (&ok);
          if (!ok || 0 >= output_device || output_device > output_devices.size ())
            {
              throw std::invalid_argument {"invalid playback device"};
            }
        }
      if (!(parser.isSet ("o") || parser.isSet ("i"))) throw std::invalid_argument {"file required"};
      if (parser.isSet ("o") && parser.isSet ("i")) throw std::invalid_argument {"specify either input or output"};

      QAudioFormat audio_format;
      if (parser.isSet ("o"))   // Record
        {
          int duration = parser.value ("d").toInt (&ok);
          if (!ok) throw std::invalid_argument {"duration not a number"};

          QFileInfo ofi {parser.value ("o")};
          if (!ofi.suffix ().size () && ofi.fileName ()[ofi.fileName ().size () - 1] != QChar {'.'})
            {
              ofi.setFile (ofi.filePath () + ".wav");
            }
          if (!parser.isSet ("f") && ofi.isFile ())
            {
              throw std::invalid_argument {"set the `-force' option to overwrite an existing output file"};
            }

          audio_format.setSampleRate (sample_rate);
          audio_format.setChannelCount (num_channels);
          audio_format.setSampleSize (16);
          audio_format.setSampleType (QAudioFormat::SignedInt);
          audio_format.setCodec ("audio/pcm");

          auto source = input_device ? input_devices[input_device - 1] : QAudioDeviceInfo::defaultInputDevice ();
          if (!source.isFormatSupported (audio_format))
            {
              qtout << "warning, requested format not supported, using nearest"
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
                    << Qt::endl
#else
                    << endl
#endif
                ;
              audio_format = source.nearestFormat (audio_format);
            }
          BWFFile output_file {audio_format, ofi.filePath ()};
          if (!output_file.open (BWFFile::WriteOnly)) throw std::invalid_argument {QString {"cannot open output file \"%1\""}.arg (ofi.filePath ()).toStdString ()};

          // run the application
          Record record {start, duration, source, &output_file, notify_interval, buffer_size};
          QObject::connect (&record, &Record::done, &app, &QCoreApplication::quit);
          return app.exec();
        }
      else                      // Playback
        {
          QFileInfo ifi {parser.value ("i")};
          if (!ifi.isFile () && !ifi.suffix ().size () && ifi.fileName ()[ifi.fileName ().size () - 1] != QChar {'.'})
            {
              ifi.setFile (ifi.filePath () + ".wav");
            }
          BWFFile input_file {audio_format, ifi.filePath ()};
          if (!input_file.open (BWFFile::ReadOnly)) throw std::invalid_argument {QString {"cannot open input file \"%1\""}.arg (ifi.filePath ()).toStdString ()};
          auto sink = output_device ? output_devices[output_device - 1] : QAudioDeviceInfo::defaultOutputDevice ();
          if (!sink.isFormatSupported (input_file.format ()))
            {
              throw std::invalid_argument {"audio output device does not support input file audio format"};
            }

          // run the application
          Playback play {start, &input_file, sink, notify_interval, buffer_size, parser.value ("category")};
          QObject::connect (&play, &Playback::done, &app, &QCoreApplication::quit);
          return app.exec();
        }
    }
  catch (std::exception const& e)
    {
      std::cerr << "Error: " << e.what () << '\n';
    }
  catch (...)
    {
      std::cerr << "Unexpected fatal error\n";
      throw; // hoping the runtime might tell us more about the exception
    }
  return -1;
}
