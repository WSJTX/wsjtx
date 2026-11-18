#include <QtMultimedia/qaudiosystemplugin.h>

#include <QSysInfo>
#include <QTimer>

#include <utility>

namespace
{
  QAudioFormat testFormat ()
  {
    QAudioFormat format;
    format.setCodec ("audio/pcm");
    format.setSampleRate (48000);
    format.setChannelCount (1);
    format.setSampleSize (16);
    format.setSampleType (QAudioFormat::SignedInt);
    format.setByteOrder (QAudioFormat::Endian (QSysInfo::ByteOrder));
    return format;
  }

  class TestDeviceInfo final : public QAbstractAudioDeviceInfo
  {
  public:
    explicit TestDeviceInfo (QByteArray device) : m_device {std::move (device)} {}

    QAudioFormat preferredFormat () const override {return testFormat ();}
    bool isFormatSupported (QAudioFormat const& format) const override
    {
      auto expected = testFormat ();
      expected.setChannelCount (format.channelCount ());
      return (format.channelCount () == 1 || format.channelCount () == 2)
        && format == expected;
    }
    QString deviceName () const override {return QStringLiteral ("WSJTX test ") + QString::fromLatin1 (m_device);}
    QStringList supportedCodecs () override {return {QStringLiteral ("audio/pcm")};}
    QList<int> supportedSampleRates () override {return {48000};}
    QList<int> supportedChannelCounts () override {return {1, 2};}
    QList<int> supportedSampleSizes () override {return {16};}
    QList<QAudioFormat::Endian> supportedByteOrders () override
    {
      return {QAudioFormat::Endian (QSysInfo::ByteOrder)};
    }
    QList<QAudioFormat::SampleType> supportedSampleTypes () override
    {
      return {QAudioFormat::SignedInt};
    }

  private:
    QByteArray const m_device;
  };

  class TestAudioOutput final : public QAbstractAudioOutput
  {
  public:
    explicit TestAudioOutput (QByteArray device) : m_device {std::move (device)}
    {
      if (m_device == "constructor-open") m_error = QAudio::OpenError;
    }

    void start (QIODevice *) override
    {
      if (m_device == "constructor-open")
        qFatal ("A backend with a construction error must not be started");
      if (m_device.endsWith ("open"))
        {
          m_state = QAudio::StoppedState;
          m_error = QAudio::OpenError;
          if (m_device == "sync-open" || m_device == "sync-clear-reset-open")
            Q_EMIT stateChanged (m_state);
          else if (m_device == "queued-open")
            QTimer::singleShot (0, this, [this] {Q_EMIT stateChanged (m_state);});
          return;
        }
      if (m_device == "fatal-start")
        {
          m_state = QAudio::StoppedState;
          m_error = QAudio::FatalError;
          return;
        }
      m_state = m_device == "idle" ? QAudio::IdleState : QAudio::ActiveState;
      m_error = QAudio::NoError;
      Q_EMIT stateChanged (m_state);
      if (m_device == "runtime-fatal")
        QTimer::singleShot (0, this, [this] {
          m_state = QAudio::StoppedState;
          m_error = QAudio::FatalError;
          Q_EMIT stateChanged (m_state);
        });
    }
    QIODevice * start () override {start (nullptr); return nullptr;}
    void stop () override {reset ();}
    void reset () override
    {
      if (m_state == QAudio::StoppedState)
        {
          if (m_device.contains ("clear-reset")) m_error = QAudio::NoError;
          return;
        }
      m_state = QAudio::StoppedState;
      m_error = QAudio::NoError;
      Q_EMIT stateChanged (m_state);
    }
    void suspend () override
    {
      m_state = QAudio::SuspendedState;
      Q_EMIT stateChanged (m_state);
    }
    void resume () override
    {
      m_state = QAudio::ActiveState;
      Q_EMIT stateChanged (m_state);
    }
    int bytesFree () const override {requirePlayback (); return qMin (m_bufferSize, 2048);}
    int periodSize () const override {requirePlayback (); return 256;}
    void setBufferSize (int value) override {m_bufferSize = value;}
    int bufferSize () const override {requirePlayback (); return m_bufferSize;}
    void setNotifyInterval (int value) override {m_notifyInterval = value;}
    int notifyInterval () const override {return m_notifyInterval;}
    qint64 processedUSecs () const override {requirePlayback (); return 250000;}
    qint64 elapsedUSecs () const override {requirePlayback (); return 500000;}
    QAudio::Error error () const override {return m_error;}
    QAudio::State state () const override {return m_state;}
    void setFormat (QAudioFormat const& format) override {m_format = format;}
    QAudioFormat format () const override {return m_format;}
    void setVolume (qreal volume) override {m_volume = volume;}
    qreal volume () const override {return m_volume;}

  private:
    void requirePlayback () const
    {
      if ((m_state != QAudio::ActiveState && m_state != QAudio::IdleState)
          || (m_error != QAudio::NoError && m_error != QAudio::UnderrunError))
        qFatal ("Unsafe playback query on the scripted audio backend");
    }

    QByteArray const m_device;
    QAudioFormat m_format;
    QAudio::State m_state {QAudio::StoppedState};
    QAudio::Error m_error {QAudio::NoError};
    int m_bufferSize {4096};
    int m_notifyInterval {1000};
    qreal m_volume {1.0};
  };
}

class SoundOutputTestPlugin final : public QAudioSystemPlugin
{
  Q_OBJECT
  Q_PLUGIN_METADATA (IID QAudioSystemFactoryInterface_iid FILE "sound_output_test_plugin.json")

public:
  QList<QByteArray> availableDevices (QAudio::Mode mode) const override
  {
    if (mode != QAudio::AudioOutput) return {};
    return {"constructor-open", "silent-open", "sync-open", "queued-open", "fatal-start",
            "clear-reset-open", "sync-clear-reset-open",
            "active", "idle", "runtime-fatal"};
  }
  QAbstractAudioInput * createInput (QByteArray const&) override {return nullptr;}
  QAbstractAudioOutput * createOutput (QByteArray const& device) override
  {
    return new TestAudioOutput {device};
  }
  QAbstractAudioDeviceInfo * createDeviceInfo (QByteArray const& device, QAudio::Mode) override
  {
    return new TestDeviceInfo {device};
  }
};

#include "sound_output_test_plugin.moc"
