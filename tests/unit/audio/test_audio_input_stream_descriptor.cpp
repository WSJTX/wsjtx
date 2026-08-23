#include <QtTest>

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QDateTime>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QVector>

#include <utility>

#include "Audio/AudioDevice.hpp"
#include "Audio/AudioStreamClock.hpp"
#include "Audio/AudioStreamDescriptor.hpp"
#include "Audio/BWFFile.hpp"
#include "Audio/FixtureAudioInput.hpp"
#include "DecDataMutex.hpp"
#include "Detector/Detector.hpp"
#include "commons.h"

namespace
{
  dec_data_t test_dec_data {};
}

dec_data_t& dec_data = test_dec_data;

extern "C" void fil4_ (qint16 *, qint32 *, qint16 *, qint32 *)
{
}

namespace
{
  QAudioFormat pcmFormat (QAudioFormat::SampleType sampleType,
                          int sampleSize, QAudioFormat::Endian byteOrder,
                          int channels)
  {
    QAudioFormat format;
    format.setByteOrder (byteOrder);
    format.setChannelCount (channels);
    format.setCodec ("audio/pcm");
    format.setSampleRate (12000);
    format.setSampleSize (sampleSize);
    format.setSampleType (sampleType);
    return format;
  }

  QString createJttyFixture ()
  {
    QTemporaryFile temp;
    temp.setAutoRemove (false);
    if (!temp.open ())
      {
        return {};
      }
    auto const path = temp.fileName ();
    temp.close ();

    BWFFile file {
      pcmFormat (QAudioFormat::SignedInt, 16,
                 QAudioFormat::LittleEndian, 1), path};
    if (!file.open (QIODevice::WriteOnly))
      {
        QFile::remove (path);
        return {};
      }
    char const samples[] = {'\0', '\0'};
    auto constexpr sampleBytes = qint64 {sizeof samples};
    if (file.write (samples, sampleBytes) != sampleBytes)
      {
        file.close ();
        QFile::remove (path);
        return {};
      }
    file.close ();
    return path;
  }

  class TestAudioSink final
    : public AudioDevice
  {
  protected:
    qint64 readData (char *, qint64) override {return -1;}
    qint64 writeData (char const *, qint64 size) override {return size;}
  };

  class TemporaryPathCleanup final
  {
  public:
    explicit TemporaryPathCleanup (QString path)
      : path_ {std::move (path)}
    {
    }

    ~TemporaryPathCleanup ()
    {
      if (!path_.isEmpty ()) QFile::remove (path_);
    }

    TemporaryPathCleanup (TemporaryPathCleanup const&) = delete;
    TemporaryPathCleanup& operator= (TemporaryPathCleanup const&) = delete;

  private:
    QString path_;
  };
}

class TestAudioInputStreamDescriptor
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void init ()
  {
    set_dec_data_input_blocked (false);
    dec_data = {};
  }

  Q_SLOT void cleanup ()
  {
    set_dec_data_input_blocked (false);
  }

  Q_SLOT void default_descriptor_is_invalid ()
  {
    AudioStreamDescriptor const descriptor;
    QVERIFY (!descriptor.isValid ());
    QCOMPARE (descriptor.sample_rate_hz, 0);
    QVERIFY (descriptor.sample_encoding
             == AudioStreamDescriptor::SampleEncoding::Unknown);
    QCOMPARE (descriptor.sample_size_bits, 0);
    QVERIFY (descriptor.byte_order == AudioStreamDescriptor::ByteOrder::Unknown);
    QCOMPARE (descriptor.channel_count, 0);
    QCOMPARE (descriptor.capture_anchor_utc_ms, qint64 {0});
    QVERIFY (descriptor.channel_layout
             == AudioStreamDescriptor::ChannelLayout::Unknown);
    QVERIFY (descriptor.clock_domain
             == AudioStreamDescriptor::ClockDomain::Unknown);
    QVERIFY (descriptor.timing_evidence
             == AudioStreamDescriptor::TimingEvidence::Unavailable);
    QVERIFY (!descriptor.can_report_discontinuities);
  }

  Q_SLOT void validity_and_equality_cover_all_facts ()
  {
    AudioStreamDescriptor descriptor;
    descriptor.sample_rate_hz = 48000;
    descriptor.sample_encoding =
      AudioStreamDescriptor::SampleEncoding::SignedInteger;
    descriptor.sample_size_bits = 16;
    descriptor.byte_order = AudioStreamDescriptor::ByteOrder::LittleEndian;
    descriptor.channel_count = 2;
    descriptor.channel_layout = AudioStreamDescriptor::ChannelLayout::Stereo;
    descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::DeviceClock;
    descriptor.timing_evidence =
      AudioStreamDescriptor::TimingEvidence::PositionCountable;

    QVERIFY (descriptor.isValid ());
    auto copy = descriptor;
    QVERIFY (copy == descriptor);
    copy.can_report_discontinuities = true;
    QVERIFY (copy != descriptor);

    copy = descriptor;
    copy.sample_rate_hz = 0;
    QVERIFY (!copy.isValid ());
    copy = descriptor;
    copy.sample_encoding = AudioStreamDescriptor::SampleEncoding::Unknown;
    QVERIFY (!copy.isValid ());
    copy = descriptor;
    copy.sample_size_bits = 0;
    QVERIFY (!copy.isValid ());
    copy = descriptor;
    copy.byte_order = AudioStreamDescriptor::ByteOrder::Unknown;
    QVERIFY (!copy.isValid ());
    copy = descriptor;
    copy.channel_count = 0;
    QVERIFY (!copy.isValid ());

    copy = descriptor;
    copy.capture_anchor_utc_ms = 123;
    QVERIFY (copy != descriptor);
  }

  Q_SLOT void selects_capture_anchor_for_system_clock ()
  {
    auto descriptor = audioStreamDescriptorFromQAudioFormat (
      pcmFormat (QAudioFormat::SignedInt, 16,
                 QAudioFormat::LittleEndian, 1));
    descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::SystemClock;
    descriptor.timing_evidence =
      AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored;
    descriptor.capture_anchor_utc_ms = 123456789000;

    AudioStreamClock clock;
    clock.setDescriptor (descriptor);
    QVERIFY (clock.reference () == AudioStreamClock::Reference::CaptureAnchor);
    QCOMPARE (clock.timestamp (987654321000), qint64 {123456789000});

    clock.advance (12000);
    QCOMPARE (clock.timestamp (987654321000), qint64 {123456790000});
  }

  Q_SLOT void uses_arrival_clock_without_capture_evidence ()
  {
    auto descriptor = audioStreamDescriptorFromQAudioFormat (
      pcmFormat (QAudioFormat::SignedInt, 16,
                 QAudioFormat::LittleEndian, 1));
    descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::DeviceClock;
    descriptor.timing_evidence =
      AudioStreamDescriptor::TimingEvidence::PositionCountable;
    descriptor.capture_anchor_utc_ms = 123456789000;

    AudioStreamClock clock;
    clock.setDescriptor (descriptor);
    QVERIFY (clock.reference () == AudioStreamClock::Reference::ArrivalWallClock);
    QCOMPARE (clock.timestamp (987654321000), qint64 {987654321000});

    descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::Unknown;
    clock.setDescriptor (descriptor);
    QVERIFY (clock.reference () == AudioStreamClock::Reference::ArrivalWallClock);
    QCOMPARE (clock.timestamp (987654321000), qint64 {987654321000});

    clock.setDescriptor ({});
    QVERIFY (clock.reference () == AudioStreamClock::Reference::ArrivalWallClock);
    QCOMPARE (clock.timestamp (987654321000), qint64 {987654321000});
  }

  Q_SLOT void maps_qt_pcm_formats ()
  {
    auto descriptor = audioStreamDescriptorFromQAudioFormat (
      pcmFormat (QAudioFormat::SignedInt, 16,
                 QAudioFormat::LittleEndian, 1));
    QVERIFY (descriptor.isValid ());
    QCOMPARE (descriptor.sample_rate_hz, 12000);
    QCOMPARE (descriptor.sample_size_bits, 16);
    QCOMPARE (descriptor.channel_count, 1);
    QVERIFY (descriptor.sample_encoding
             == AudioStreamDescriptor::SampleEncoding::SignedInteger);
    QVERIFY (descriptor.byte_order
             == AudioStreamDescriptor::ByteOrder::LittleEndian);
    QVERIFY (descriptor.channel_layout
             == AudioStreamDescriptor::ChannelLayout::Mono);

    descriptor = audioStreamDescriptorFromQAudioFormat (
      pcmFormat (QAudioFormat::UnSignedInt, 8,
                 QAudioFormat::LittleEndian, 6));
    QVERIFY (descriptor.isValid ());
    QCOMPARE (descriptor.sample_rate_hz, 12000);
    QCOMPARE (descriptor.sample_size_bits, 8);
    QCOMPARE (descriptor.channel_count, 6);
    QVERIFY (descriptor.sample_encoding
             == AudioStreamDescriptor::SampleEncoding::UnsignedInteger);
    QVERIFY (descriptor.byte_order == AudioStreamDescriptor::ByteOrder::Unknown);
    QVERIFY (descriptor.channel_layout
             == AudioStreamDescriptor::ChannelLayout::Unknown);

    descriptor = audioStreamDescriptorFromQAudioFormat (
      pcmFormat (QAudioFormat::Float, 32, QAudioFormat::BigEndian, 2));
    QVERIFY (descriptor.isValid ());
    QCOMPARE (descriptor.sample_rate_hz, 12000);
    QCOMPARE (descriptor.sample_size_bits, 32);
    QCOMPARE (descriptor.channel_count, 2);
    QVERIFY (descriptor.sample_encoding
             == AudioStreamDescriptor::SampleEncoding::FloatingPoint);
    QVERIFY (descriptor.byte_order
             == AudioStreamDescriptor::ByteOrder::BigEndian);
    QVERIFY (descriptor.channel_layout
             == AudioStreamDescriptor::ChannelLayout::Stereo);
  }

  Q_SLOT void rejects_unrecognized_qt_formats ()
  {
    QVERIFY (!audioStreamDescriptorFromQAudioFormat (QAudioFormat {}).isValid ());

    auto unknown = pcmFormat (QAudioFormat::Unknown, 16,
                              QAudioFormat::LittleEndian, 1);
    QVERIFY (!audioStreamDescriptorFromQAudioFormat (unknown).isValid ());

    auto nonPcm = pcmFormat (QAudioFormat::SignedInt, 16,
                             QAudioFormat::LittleEndian, 1);
    nonPcm.setCodec ("audio/example");
    QVERIFY (!audioStreamDescriptorFromQAudioFormat (nonPcm).isValid ());

    auto nonByteAligned = pcmFormat (QAudioFormat::SignedInt, 7,
                                     QAudioFormat::LittleEndian, 1);
    QVERIFY (!audioStreamDescriptorFromQAudioFormat (nonByteAligned).isValid ());
  }

  Q_SLOT void fixture_reports_opened_stream ()
  {
    auto const path = createJttyFixture ();
    QVERIFY (!path.isEmpty ());
    TemporaryPathCleanup cleanup {path};

    FixtureAudioInput source {path, FixtureAudioInput::Profile::Jtty};
    TestAudioSink sink;
    QVector<AudioStreamDescriptor> changes;
    connect (&source, &AudioInputSource::streamDescriptorChanged,
             [&changes] (AudioStreamDescriptor descriptor) {
               changes.append (descriptor);
             });

    QVERIFY (!source.streamDescriptor ().isValid ());
    source.start (QAudioDeviceInfo {}, 0, &sink, 1, AudioDevice::Mono);

    QCOMPARE (changes.size (), 1);
    auto const descriptor = source.streamDescriptor ();
    QVERIFY (descriptor.isValid ());
    QCOMPARE (descriptor.sample_rate_hz, 12000);
    QVERIFY (descriptor.sample_encoding
             == AudioStreamDescriptor::SampleEncoding::SignedInteger);
    QCOMPARE (descriptor.sample_size_bits, 16);
    QVERIFY (descriptor.byte_order
             == AudioStreamDescriptor::ByteOrder::LittleEndian);
    QCOMPARE (descriptor.channel_count, 1);
    QVERIFY (descriptor.channel_layout
             == AudioStreamDescriptor::ChannelLayout::Mono);
    QVERIFY (descriptor.clock_domain
             == AudioStreamDescriptor::ClockDomain::SystemClock);
    QVERIFY (descriptor.timing_evidence
             == AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored);
    QCOMPARE (descriptor.capture_anchor_utc_ms, qint64 {0});
    QVERIFY (!descriptor.can_report_discontinuities);
    QVERIFY (changes.constFirst () == descriptor);

    auto constexpr fixturePeriodMs = qint64 {180000};
    auto const periodOffset = QDateTime::currentMSecsSinceEpoch ()
      % fixturePeriodMs;
    auto const untilNextPeriod = fixturePeriodMs - periodOffset;
    if (untilNextPeriod < 3000)
      {
        QTest::qWait (static_cast<int> (untilNextPeriod + 100));
      }

    QSignalSpy emissions {&source, &FixtureAudioInput::emissionStarted};
    source.resume ();
    source.arm ();
    auto const anchored = source.streamDescriptor ();
    QVERIFY (anchored.capture_anchor_utc_ms > 0);
    QCOMPARE (changes.size (), 2);
    QVERIFY (changes.constLast () == anchored);

    QTRY_VERIFY_WITH_TIMEOUT (emissions.count () > 0, 1000);
    QCOMPARE (emissions.constFirst ().constFirst ().toLongLong (),
              anchored.capture_anchor_utc_ms);
    QTest::qWait (200);
    source.suspend ();
    auto const beforeResume = QDateTime::currentMSecsSinceEpoch ();
    source.resume ();
    auto const resumed = source.streamDescriptor ();
    QVERIFY (resumed.capture_anchor_utc_ms >= beforeResume - 20);
    QVERIFY (resumed.capture_anchor_utc_ms <= beforeResume + 20);
    QCOMPARE (changes.size (), 3);

    source.stop ();
    QCOMPARE (changes.size (), 4);
    QVERIFY (!changes.constLast ().isValid ());
    QVERIFY (!source.streamDescriptor ().isValid ());
  }

  Q_SLOT void detector_clock_advances_for_consumed_frames ()
  {
    Detector detector {12000, 1.0, 1};
    QVERIFY (detector.initialize (QIODevice::WriteOnly, AudioDevice::Mono));

    auto descriptor = audioStreamDescriptorFromQAudioFormat (
      pcmFormat (QAudioFormat::SignedInt, 16,
                 QAudioFormat::LittleEndian, 1));
    descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::SystemClock;
    descriptor.timing_evidence =
      AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored;
    descriptor.capture_anchor_utc_ms = 100500;
    detector.setStreamDescriptor (descriptor);

    QByteArray accepted {2400, '\0'};
    QCOMPARE (detector.write (accepted.constData (), accepted.size ()),
              qint64 {accepted.size ()});
    QCOMPARE (dec_data.params.kin, qint64 {1200});

    QByteArray blocked {14400, '\0'};
    set_dec_data_input_blocked (true);
    QCOMPARE (detector.write (blocked.constData (), blocked.size ()),
              qint64 {blocked.size ()});
    set_dec_data_input_blocked (false);

    QByteArray finalFrame {2, '\0'};
    QCOMPARE (detector.write (finalFrame.constData (), finalFrame.size ()),
              qint64 {finalFrame.size ()});
    QCOMPARE (dec_data.params.kin, qint64 {1});

    detector.setStreamDescriptor (descriptor);
    auto const capacity = sizeof dec_data.d2 / sizeof dec_data.d2[0];
    dec_data.params.kin = static_cast<qint64> (capacity - 1);

    QByteArray overflow {14400, '\0'};
    QCOMPARE (detector.write (overflow.constData (), overflow.size ()),
              qint64 {overflow.size ()});
    QCOMPARE (dec_data.params.kin, static_cast<qint64> (capacity));

    QCOMPARE (detector.write (finalFrame.constData (), finalFrame.size ()),
              qint64 {finalFrame.size ()});
    QCOMPARE (dec_data.params.kin, qint64 {1});
  }
};

QTEST_GUILESS_MAIN (TestAudioInputStreamDescriptor)

#include "test_audio_input_stream_descriptor.moc"
