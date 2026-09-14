#include <QtTest>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "Audio/AudioDevice.hpp"
#include "DecDataMutex.hpp"
#include "Detector/Detector.hpp"
#include "commons.h"
#include "ReferenceSpectrum.hpp"
#include "wsjtx_config.h"

extern "C" void refspectrum_ (short *, int *, bool *, bool *, bool *, char const *, fortran_charlen_t);

namespace
{
dec_data_t storage {};
constexpr int block = 3584;
constexpr int committed = 2 * block;
constexpr short patternA = 1234;
constexpr short patternB = 5678;

// Both conflicting accesses take place AFTER this rendezvous. In particular,
// there is no producer-completion acknowledgement before the DSP read.
class Rendezvous
{
public:
  bool arrive ()
  {
    std::unique_lock<std::mutex> lock {mutex_};
    ++arrivals_;
    changed_.notify_all ();
    return changed_.wait_for (lock, std::chrono::seconds {5},
                              [this] { return arrivals_ == 2; });
  }
private:
  std::mutex mutex_;
  std::condition_variable changed_;
  int arrivals_ = 0;
};

AudioStreamDescriptor descriptor (bool rollover)
{
  AudioStreamDescriptor result;
  result.sample_rate_hz = 12000;
  result.sample_encoding = AudioStreamDescriptor::SampleEncoding::SignedInteger;
  result.sample_size_bits = 16;
  result.byte_order = AudioStreamDescriptor::ByteOrder::LittleEndian;
  result.channel_count = 1;
  result.channel_layout = AudioStreamDescriptor::ChannelLayout::Mono;
  result.clock_domain = AudioStreamDescriptor::ClockDomain::SystemClock;
  result.timing_evidence = AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored;
  // With a one-second period, 7168 samples advance 100.500 s to 101.097 s.
  // Detector's next write therefore takes its real period-reset path.
  result.capture_anchor_utc_ms = rollover ? 100500 : 90000;
  return result;
}

bool writeSamples (Detector& detector, int count, short value)
{
  std::vector<short> samples (count, value);
  auto const bytes = qint64 (samples.size () * sizeof (short));
  return detector.write (reinterpret_cast<char const *> (samples.data ()), bytes) == bytes;
}

bool writeReferenceSpectrum (QFile& file)
{
  if (!file.open (QIODevice::WriteOnly)) return false;
  file.write (QString::asprintf ("%5d%5d%5d", 400, 2600, 5).toLatin1 ());
  for (int i = 0; i < 5; ++i)
    file.write (QString::asprintf ("%25.16e", 0.).toLatin1 ());
  file.write ("\n");
  for (int i = 1; i <= 3456; ++i)
    file.write (QString::asprintf ("%10.3f%12.3e%12.6f%12.6f%12.6f\n",
      12000. * i / 6912., 1., 0., 0.5, -6.0206).toLatin1 ());
  file.close ();
  return true;
}

std::vector<short> noiseSamples (int count)
{
  std::vector<short> samples (count);
  unsigned noise = 1;
  for (auto& sample : samples)
    {
      noise = 1664525u * noise + 1013904223u;
      sample = short (int (noise >> 20) - 2048);
    }
  return samples;
}

std::vector<short> runReferenceSpectrum (std::vector<short> const& original,
                                         int step, bool measure,
                                         QByteArray const& path)
{
  auto samples = original;
  bool clear = true, apply = !measure;
  int zero = 0;
  refspectrum_ (samples.data (), &zero, &clear, &measure, &apply,
                path.constData (), fortran_charlen_t (path.size ()));
  clear = false;
  ReferenceSpectrumInput reference;
  for (int end = std::min (step, int (samples.size ())); ;
       end = std::min (end + step, int (samples.size ())))
    {
      reference.consume (samples.data (), end, measure ? 1 : 2,
        [&] (short * p, int count) {
          refspectrum_ (p, &count, &clear, &measure, &apply,
                        path.constData (), fortran_charlen_t (path.size ()));
        });
      if (end == int (samples.size ())) break;
    }
  return samples;
}
}

dec_data_t& dec_data = storage;
extern "C" void receive_reference_probe (short *, int);
extern "C" void receive_reference_apply_probe (short *, int);
extern "C" void fil4_state_ (qint16 *, qint32 *, qint16 *, qint32 *, float *);
extern "C" void save_echo_params_ (int *, int *, int *, float *, float *,
                                   int *, int *, short *, int *);

struct EchoMetadata
{
  int total = 123456, audio = 789, rit = 123, spacing = 15;
  float frequency = 1500.5f, spread = 2.5f;
  std::array<int, 6> tones {{0, 1, 2, 3, 4, 5}};

  void transfer (short * samples, int direction)
  {
    save_echo_params_ (&total, &audio, &rit, &frequency, &spread,
                       &spacing, tones.data (), samples, &direction);
  }
};

class TestReceiveAudioHandoff : public QObject
{
  Q_OBJECT

  void notificationTest (bool rollover)
  {
    Detector detector {12000, 1.0, 1};
    detector.setBlockSize (block);
    auto * guiThread = QThread::currentThread ();
    std::vector<short> observed;
    std::vector<qint64> counts;
    auto received = std::make_unique<dec_data_t> ();
    ReceiveAudioConsumer consumer;
    int rejected = 0;
    connect (&detector, &Detector::audioBlock, this, [&] (ReceiveAudio audio) {
      if (!consumer.accept (audio, *received)) { ++rejected; return; }
      counts.push_back (audio->end ());
      observed.push_back (received->d2[audio->end () - 1]);
    }, Qt::QueuedConnection);

    bool wrote = false;
    std::unique_ptr<QThread> worker {QThread::create ([&] {
      bool ok = detector.initialize (QIODevice::WriteOnly, AudioDevice::Mono);
      detector.setStreamDescriptor (descriptor (true));
      ok = writeSamples (detector, committed, patternA) && ok;
      if (rollover) ok = writeSamples (detector, committed, patternB) && ok;
      wrote = ok;
      detector.moveToThread (guiThread);
    })};
    detector.moveToThread (worker.get ());
    worker->start ();
    // Intentionally withhold GUI delivery until *all* writes have completed.
    // There is no data race here: the assertion checks period identity.
    worker->wait ();
    QCoreApplication::sendPostedEvents (this, QEvent::MetaCall);
    QVERIFY (wrote);
    QCOMPARE (counts.size (), std::size_t (2));
    QCOMPARE (rejected, rollover ? 2 : 0);
    QCOMPARE (counts[0], qint64 (block));
    QCOMPARE (counts[1], qint64 (committed));
    QCOMPARE (observed[0], rollover ? patternB : patternA);
    QCOMPARE (observed[1], rollover ? patternB : patternA);
  }

  void consumerRace (bool rollover, bool echo = false)
  {
    int const initialFrames = echo ? 8 * block : committed;
    Detector detector {12000, rollover ? (echo ? 3.0 : 1.0) : 15.0, 1};
    detector.setBlockSize (block);
    auto * guiThread = QThread::currentThread ();
    Rendezvous start;
    bool consumed = false;
    bool consumerReady = false;
    bool producerReady = false;
    bool wrote = false;
    auto received = std::make_unique<dec_data_t> ();
    ReceiveAudioConsumer consumer;
    QEventLoop events;
    QTimer timeout;
    timeout.setSingleShot (true);
    connect (&timeout, &QTimer::timeout, &events, &QEventLoop::quit);
    connect (&detector, &Detector::audioBlock, this, [&] (ReceiveAudio audio) {
      if (consumed || !consumer.accept (audio, *received)) return;
      int const frames = audio->end ();
      if (frames != initialFrames) return;
      consumed = true;
      // Use the production committed-range adapter, independent of MSK's
      // 3584-sample notification step or symbol length.
      auto * input = echo ? received->d2
                         : received->d2 + frames - 3456;
      consumerReady = start.arrive ();
      if (consumerReady)
        {
          if (echo)
            {
              EchoMetadata metadata;
              metadata.transfer (input, 1);
            }
          else
            {
              ReferenceSpectrumInput reference;
              reference.consume (input, 3456, 1, receive_reference_probe);
            }
        }
    }, Qt::QueuedConnection);

    std::unique_ptr<QThread> worker {QThread::create ([&] {
      bool ok = detector.initialize (QIODevice::WriteOnly, AudioDevice::Mono);
      detector.setStreamDescriptor (descriptor (rollover));
      ok = writeSamples (detector, initialFrames, patternA) && ok;
      producerReady = start.arrive ();
      if (producerReady) ok = writeSamples (detector, block, patternB) && ok;
      wrote = ok;
      detector.moveToThread (guiThread);
    })};
    connect (worker.get (), &QThread::finished, &events, &QEventLoop::quit);
    detector.moveToThread (worker.get ());
    timeout.start (15000);
    worker->start ();
    events.exec ();
    worker->wait ();
    QCoreApplication::sendPostedEvents (this, QEvent::MetaCall);
    QVERIFY (wrote);
    QVERIFY (consumed);
    QVERIFY (producerReady);
    QVERIFY (consumerReady);
    QVERIFY (std::all_of (received->d2 + (echo ? 15 : 0),
                         received->d2 + initialFrames,
                         [] (short x) { return x == patternA; }));
    if (echo)
      {
        EchoMetadata actual;
        actual.total = actual.audio = actual.rit = actual.spacing = 0;
        actual.frequency = actual.spread = 0;
        actual.tones.fill (0);
        actual.transfer (received->d2, -1);
        EchoMetadata expected;
        QCOMPARE (actual.total, expected.total);
        QCOMPARE (actual.audio, expected.audio);
        QCOMPARE (actual.rit, expected.rit);
        QCOMPARE (actual.spacing, expected.spacing);
        QCOMPARE (actual.frequency, expected.frequency);
        QCOMPARE (actual.spread, expected.spread);
        QVERIFY (actual.tones == expected.tones);
        // Header insertion must leave all captured audio after sample 15 intact.
        QVERIFY (std::all_of (received->d2 + 15, received->d2 + initialFrames,
                             [] (short x) { return x == patternA; }));
      }
    // TSan checks conflicting accesses when the compiler instruments them.
    // See echo-jtty-ownership.md for the local Echo array-copy blind spot.
    QCOMPARE (received->params.kin, initialFrames);
  }

private Q_SLOTS:
  void downsamplersRetainIndependentHistory ()
  {
    std::array<float, 49> firstState {}, secondState {}, referenceState {};
    std::array<qint16, 16> firstInput, secondInput, interferingInput;
    firstInput.fill (1200);
    secondInput.fill (2400);
    interferingInput.fill (-3000);
    std::array<qint16, 4> ignored {}, actual {}, expected {};
    qint32 inputFrames = int (firstInput.size ());
    qint32 outputFrames = 0;

    fil4_state_ (firstInput.data (), &inputFrames, ignored.data (),
                 &outputFrames, firstState.data ());
    fil4_state_ (interferingInput.data (), &inputFrames, ignored.data (),
                 &outputFrames, secondState.data ());
    fil4_state_ (secondInput.data (), &inputFrames, actual.data (),
                 &outputFrames, firstState.data ());
    fil4_state_ (firstInput.data (), &inputFrames, ignored.data (),
                 &outputFrames, referenceState.data ());
    fil4_state_ (secondInput.data (), &inputFrames, expected.data (),
                 &outputFrames, referenceState.data ());

    QVERIFY (actual == expected);
  }

  void producersRemainIsolatedAcrossSourceSwitch ()
  {
    ReceiveAudioProducer first;
    ReceiveAudioProducer second;
    ReceiveAudio firstPrefix, firstContinuation, secondPrefix, secondContinuation;
    {
      QMutexLocker lock {&dec_data_mutex ()};
      first.reset (15.0);
      std::fill_n (first.data ().d2, block, patternA);
      first.setFrames (block);
      firstPrefix = first.capture (block, 15.0);

      second.reset (15.0);
      std::fill_n (second.data ().d2, block, patternB);
      second.setFrames (block);
      secondPrefix = second.capture (block, 15.0);

      std::fill_n (first.data ().d2 + block, block, patternA);
      first.setFrames (committed);
      firstContinuation = first.capture (committed, 15.0);
      std::fill_n (second.data ().d2 + block, block, patternB);
      second.setFrames (committed);
      secondContinuation = second.capture (committed, 15.0);
    }

    QCOMPARE (first.frames (), committed);
    QCOMPARE (second.frames (), committed);
    QCOMPARE (firstContinuation->start, block);
    QCOMPARE (secondContinuation->start, block);
    QVERIFY (std::all_of (firstContinuation->samples.begin (),
                          firstContinuation->samples.end (),
                          [] (short sample) { return sample == patternA; }));
    QVERIFY (std::all_of (secondContinuation->samples.begin (),
                          secondContinuation->samples.end (),
                          [] (short sample) { return sample == patternB; }));

    auto target = std::make_unique<dec_data_t> ();
    ReceiveAudioConsumer consumer;
    QVERIFY (consumer.accept (firstPrefix, *target));
    QVERIFY (consumer.accept (secondPrefix, *target));
    QVERIFY (!consumer.accept (firstContinuation, *target));
    QVERIFY (consumer.accept (secondContinuation, *target));
    QCOMPARE (target->params.kin, committed);
    QCOMPARE (target->d2[committed - 1], patternB);
  }

  void handoff_rejects_gaps_and_disk_stale_work ()
  {
    Detector detector {12000, 15.0, 1};
    detector.setBlockSize (block);
    QVERIFY (detector.initialize (QIODevice::WriteOnly, AudioDevice::Mono));
    detector.setStreamDescriptor (descriptor (false));
    std::vector<ReceiveAudio> blocks;
    connect (&detector, &Detector::audioBlock, this,
             [&] (ReceiveAudio audio) { blocks.push_back (std::move (audio)); });
    QVERIFY (writeSamples (detector, committed, patternA));
    QCOMPARE (blocks.size (), std::size_t (2));
    ReceiveAudioConsumer consumer;
    auto received = std::make_unique<dec_data_t> ();
    QVERIFY (!consumer.accept (blocks[1], *received)); // missing prefix
    QCOMPARE (received->params.kin, 0);
    QVERIFY (consumer.accept (blocks[0], *received));
    QVERIFY (!consumer.accept (blocks[0], *received)); // duplicate
    QVERIFY (consumer.accept (blocks[1], *received));
    QCOMPARE (received->params.kin, committed);
    // Installing a WAV invalidates all queued work from the old live epoch.
    consumer.invalidate ();
    std::fill_n (received->d2, committed, short {-123});
    QVERIFY (!consumer.accept (blocks[0], *received));
    QCOMPARE (received->d2[0], short {-123});
    detector.reset ();
    QVERIFY (writeSamples (detector, block, patternB));
    QCOMPARE (blocks.size (), std::size_t (3));
    QVERIFY (consumer.accept (blocks.back (), *received));
    QCOMPARE (received->params.kin, block);
    QVERIFY (std::all_of (received->d2, received->d2 + block,
                         [] (short x) { return x == patternB; }));
    QVERIFY (std::all_of (received->d2 + block, received->d2 + committed,
                         [] (short x) { return x == 0; }));
    // Producer reset and consumer writes cannot change an already owned block.
    QVERIFY (std::all_of (blocks[0]->samples.begin (), blocks[0]->samples.end (),
                         [] (short x) { return x == patternA; }));
    detector.setTRPeriod (30.0);
    QVERIFY (writeSamples (detector, block, patternA));
    QCOMPARE (blocks.size (), std::size_t (4));
    QVERIFY (!consumer.accept (blocks[2], *received));
    QVERIFY (consumer.accept (blocks[3], *received));
    QCOMPARE (received->params.kin, block);
    QCOMPARE (received->d2[0], patternA);
    QVERIFY (writeSamples (detector, 288, patternB));
    QCOMPARE (blocks.size (), std::size_t (4)); // incomplete notification
    detector.flushBufferedFrames (block + 288);
    QCOMPARE (blocks.size (), std::size_t (5));
    QVERIFY (consumer.accept (blocks.back (), *received));
    QCOMPARE (received->params.kin, block + 288);
    QCOMPARE (received->d2[block + 287], patternB);
    detector.flushBufferedFrames (block + 288);
    QCOMPARE (blocks.size (), std::size_t (5)); // flush is idempotent
  }
  void stale_echo_metadata_must_not_modify_next_period ()
  {
    constexpr int echoFrames = 8 * block;
    Detector detector {12000, 3.0, 1};
    detector.setBlockSize (block);
    auto * guiThread = QThread::currentThread ();
    bool handled = false, wrote = false;
    auto received = std::make_unique<dec_data_t> ();
    ReceiveAudioConsumer consumer;
    int rejected = 0;
    connect (&detector, &Detector::audioBlock, this, [&] (ReceiveAudio audio) {
      if (!consumer.accept (audio, *received)) { ++rejected; return; }
      if (audio->end () != echoFrames) return;
      handled = true; // B is still processed; stale A cannot change it.
      EchoMetadata metadata;
      metadata.transfer (received->d2, 1);
    }, Qt::QueuedConnection);
    std::unique_ptr<QThread> worker {QThread::create ([&] {
      bool ok = detector.initialize (QIODevice::WriteOnly, AudioDevice::Mono);
      detector.setStreamDescriptor (descriptor (true));
      ok = writeSamples (detector, echoFrames, patternA) && ok;
      ok = writeSamples (detector, echoFrames, patternB) && ok;
      wrote = ok;
      detector.moveToThread (guiThread);
    })};
    detector.moveToThread (worker.get ());
    worker->start ();
    worker->wait ();
    QCoreApplication::sendPostedEvents (this, QEvent::MetaCall);
    QVERIFY (wrote);
    QVERIFY (handled);
    QCOMPARE (rejected, 8);
    QCOMPARE (received->params.kin, echoFrames);
    QVERIFY (std::all_of (received->d2 + 15, received->d2 + echoFrames,
                         [] (short x) { return x == patternB; }));
  }

  void queued_prefix_without_reuse () { notificationTest (false); }
  void queued_period_must_retain_identity () { notificationTest (true); }
  void reference_measurement_vs_append () { consumerRace (false); }
  void reference_measurement_vs_reset () { consumerRace (true); }
  void reference_application_preserves_uncommitted_suffix ()
  {
    std::fill_n (dec_data.d2, committed, patternA);
    dec_data.params.kin = committed;
    std::fill_n (dec_data.d2 + committed, 3456, patternB);
    std::array<short, 3456> suffix;
    std::copy_n (dec_data.d2 + committed, suffix.size (), suffix.begin ());

    ReferenceSpectrumInput reference;
    reference.consume (dec_data.d2, committed, 2, receive_reference_apply_probe);
    QVERIFY (std::equal (suffix.begin (), suffix.end (),
                         dec_data.d2 + committed));
  }
  void reference_input_packetization ()
  {
    std::vector<short> samples (13952);
    std::vector<std::pair<int, int>> calls;
    ReferenceSpectrumInput reference;
    auto record = [&] (short * p, int count) {
      calls.emplace_back (int (p - samples.data ()), count);
    };

    reference.consume (samples.data (), 128, 2, record);
    reference.consume (samples.data (), 2176, 2, record);
    reference.consume (samples.data (), 5760, 2, record);
    reference.consume (samples.data (), 13952, 2, record);
    reference.consume (samples.data (), 13952, 2, record);

    std::vector<std::pair<int, int>> const expected {
      {0, 0}, {0, 128}, {128, 2048}, {2176, 3456}, {5632, 128},
      {5760, 3456}, {9216, 3456}, {12672, 1280}
    };
    QVERIFY (calls == expected);

    calls.clear ();
    reference.consume (samples.data (), 32, 2, record);
    std::vector<std::pair<int, int>> const newPeriod {{0, 0}, {0, 32}};
    QVERIFY (calls == newPeriod);

    calls.clear ();
    reference.reset ();
    reference.consume (samples.data (), 64, 2, record);
    std::vector<std::pair<int, int>> const reset {{0, 0}, {0, 64}};
    QVERIFY (calls == reset);
  }
  void reference_application_packetization ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto path = (directory.path () + "/refspec.dat").toLocal8Bit ();
    QFile file (QString::fromLocal8Bit (path));
    QVERIFY (writeReferenceSpectrum (file));
    auto const original = noiseSamples (2 * 3456);
    auto const baseline = runReferenceSpectrum (original, 3456, false, path);
    QCOMPARE (baseline.size (), original.size ());
    QVERIFY (baseline != original);
    QVERIFY (std::any_of (baseline.begin (), baseline.end (), [] (short s) {return s != 0;}));
    auto const output = runReferenceSpectrum (original, 3584, false, path);
    QCOMPARE (output.size (), baseline.size ());
    for (std::size_t i = 0; i < output.size (); ++i)
      QVERIFY (std::abs (int (output[i]) - int (baseline[i])) <= 1);
  }
  void reference_measurement_packetization ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto path = (directory.path () + "/refspec.dat").toLocal8Bit ();
    QFile file (QString::fromLocal8Bit (path));
    auto const original = noiseSamples (4 * 3456);

    QCOMPARE (runReferenceSpectrum (original, 3456, true, path), original);
    QVERIFY (file.open (QIODevice::ReadOnly));
    auto const measured = file.readAll ();
    file.close ();
    QVERIFY (!measured.isEmpty ());
    QCOMPARE (runReferenceSpectrum (original, 3584, true, path), original);
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.readAll (), measured);
  }
  void echo_metadata_vs_append () { consumerRace (false, true); }
  void echo_metadata_vs_reset () { consumerRace (true, true); }
};

QTEST_GUILESS_MAIN (TestReceiveAudioHandoff)
#include "test_receive_audio_handoff.moc"
