#include <QtTest>
#include <QThread>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "FastDecode.hpp"
#include "Detector/Detector.hpp"
#include "commons.h"
#include "wsjtx_config.h"

namespace
{
dec_data_t storage {};
constexpr int capacity = 360000;
constexpr int committed = 7168;

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

struct DecoderProbe
{
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false;
  bool released = false;
  bool timedOut = false;
  bool validCount = false;
  bool offCallerThread = false;
  QThread * callerThread = QThread::currentThread ();
  std::vector<short> samples;
  std::array<int, 15> arguments;
  double period = 0;
  QByteArray myCall, hisCall;
  std::array<fortran_charlen_t, 3> lengths;
  Rendezvous * sampleAccessStart = nullptr;

  bool waitForEntry ()
  {
    std::unique_lock<std::mutex> lock {mutex};
    return changed.wait_for (lock, std::chrono::seconds {5},
                             [this] { return entered; });
  }

  void release ()
  {
    std::lock_guard<std::mutex> lock {mutex};
    released = true;
    changed.notify_all ();
  }
};

DecoderProbe * activeProbe = nullptr;
std::array<DecoderProbe *, 2> overlappingProbes {{nullptr, nullptr}};

struct Inputs
{
  std::array<int, 15> arguments {};
  double period = 15;
  std::array<char, 8000> messages {};
  std::array<char, 12> myCall, hisCall;

  Inputs ()
  {
    arguments[0] = 123000;
    arguments[1] = committed;
    arguments[3] = 1;
    arguments[8] = 2;
    arguments[9] = 104;
    arguments[10] = 1500;
    arguments[11] = 100;
    std::memcpy (myCall.data (), "K1ABC       ", 12);
    std::memcpy (hisCall.data (), "W9XYZ       ", 12);
  }

  QFuture<FastDecodeResult> submit (short const * samples = storage.d2)
  {
    return startFastDecode (samples, arguments.data (), period,
                            myCall.data (), hisCall.data ());
  }
};
}

dec_data_t& dec_data = storage;

// Substitute only the decoder at its ABI boundary. The production helper
// still copies the source and launches the real QtConcurrent job. Delaying
// input consumption models an ordinarily scheduled-but-not-yet-running job.
extern "C" void fast_decode_ (short samples[], int arguments[], double * period,
                               char messages[], char myCall[], char hisCall[],
                               fortran_charlen_t messageLength,
                               fortran_charlen_t myCallLength,
                               fortran_charlen_t hisCallLength)
{
  auto * selected = activeProbe;
  if (!selected && arguments[0] == 111000) selected = overlappingProbes[0];
  if (!selected && arguments[0] == 222000) selected = overlappingProbes[1];
  auto& probe = *selected;
  {
    std::unique_lock<std::mutex> lock {probe.mutex};
    probe.entered = true;
    probe.changed.notify_all ();
    probe.timedOut = !probe.changed.wait_for (lock, std::chrono::seconds {5},
                                             [&] { return probe.released; });
  }
  if (probe.timedOut) return; // A harness timeout must not introduce an unordered read.
  if (probe.sampleAccessStart && !probe.sampleAccessStart->arrive ())
    {
      probe.timedOut = true;
      return;
    }
  probe.offCallerThread = QThread::currentThread () != probe.callerThread;
  probe.validCount = arguments[1] >= 0 && arguments[1] <= capacity;
  if (probe.validCount) probe.samples.assign (samples, samples + arguments[1]);
  std::copy_n (arguments, 15, probe.arguments.begin ());
  probe.period = *period;
  probe.lengths = {{messageLength, myCallLength, hisCallLength}};
  probe.myCall = QByteArray (myCall, 12);
  probe.hisCall = QByteArray (hisCall, 12);
  // Preserve the existing ABI's output behavior, including in/out arguments.
  std::memcpy (messages, "PROBE", 6);
  arguments[12] = 31;
  arguments[13] = 248;
}

class TestFastDecodeOwnership : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void captures_submission_inputs_data ()
  {
    QTest::addColumn<int> ("mutation");
    QTest::newRow ("unchanged") << 0;
    QTest::newRow ("samples") << 1;
    QTest::newRow ("mycall") << 2;
    QTest::newRow ("hiscall") << 3;
    QTest::newRow ("period") << 4;
    QTest::newRow ("arguments") << 5;
  }

  void captures_submission_inputs ()
  {
    QFETCH (int, mutation);
    std::fill_n (storage.d2, capacity, short (1234));
    Inputs input;
    auto const expectedArguments = input.arguments;
    DecoderProbe probe;
    activeProbe = &probe;
    auto future = input.submit ();
    bool const entered = probe.waitForEntry ();
    // All caller storage remains alive. The release orders these mutations
    // before the probe reads, so an assertion failure is an ownership defect,
    // not undefined behavior deliberately introduced by the test.
    if (mutation == 1) std::fill_n (storage.d2, capacity, short (5678));
    if (mutation == 2) std::memcpy (input.myCall.data (), "N0NEW       ", 12);
    if (mutation == 3) std::memcpy (input.hisCall.data (), "G4NEW       ", 12);
    if (mutation == 4) input.period = 30;
    if (mutation == 5) input.arguments[10] = 2500;
    probe.release ();
    future.waitForFinished ();
    activeProbe = nullptr;

    QVERIFY (entered);
    QVERIFY (!probe.timedOut);
    QVERIFY (probe.offCallerThread);
    QVERIFY (probe.validCount);
    QCOMPARE (probe.lengths[0], fortran_charlen_t (8000));
    QCOMPARE (probe.lengths[1], fortran_charlen_t (12));
    QCOMPARE (probe.lengths[2], fortran_charlen_t (12));
    auto const result = future.result ();
    QCOMPARE (QByteArray (result.messages.data ()), QByteArray ("PROBE"));
    QCOMPARE (result.arguments[12], 31);
    QCOMPARE (result.arguments[13], 248);
    QCOMPARE (input.arguments[12], 0); // worker never writes caller storage
    QVERIFY (probe.arguments == expectedArguments);
    QCOMPARE (probe.samples.size (), std::size_t (committed));
    QVERIFY (std::all_of (probe.samples.begin (), probe.samples.end (),
                         [] (short x) { return x == 1234; }));
    QCOMPARE (probe.myCall, QByteArray ("K1ABC       ", 12));
    QCOMPARE (probe.hisCall, QByteArray ("W9XYZ       ", 12));
    QCOMPARE (probe.period, 15.0);
  }

  void capture_does_not_race_live_append ()
  {
    Detector detector {12000, 15.0, 1};
    QVERIFY (detector.initialize (QIODevice::WriteOnly, AudioDevice::Mono));
    AudioStreamDescriptor descriptor;
    descriptor.sample_rate_hz = 12000;
    descriptor.sample_encoding = AudioStreamDescriptor::SampleEncoding::SignedInteger;
    descriptor.sample_size_bits = 16;
    descriptor.byte_order = AudioStreamDescriptor::ByteOrder::LittleEndian;
    descriptor.channel_count = 1;
    descriptor.channel_layout = AudioStreamDescriptor::ChannelLayout::Mono;
    descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::SystemClock;
    descriptor.timing_evidence = AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored;
    descriptor.capture_anchor_utc_ms = 90000;
    detector.setStreamDescriptor (descriptor);
    ReceiveAudioConsumer consumer;
    auto connection = connect (&detector, &Detector::audioBlock, this,
      [&] (ReceiveAudio audio) { QVERIFY (consumer.accept (audio, storage)); });
    std::vector<short> initial (committed, 1234);
    QCOMPARE (detector.write (reinterpret_cast<char const *> (initial.data ()),
                             initial.size () * sizeof (short)), qint64 (2 * committed));
    disconnect (connection);

    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false, released = false, writerStarted = false;
    qint64 written = -1;
    auto * callerThread = QThread::currentThread ();
    std::unique_ptr<QThread> writer {QThread::create ([&] {
      {
        std::unique_lock<std::mutex> lock {mutex};
        ready = true;
        changed.notify_all ();
        writerStarted = changed.wait_for (lock, std::chrono::seconds {5},
                                          [&] { return released; });
      }
      if (writerStarted)
        {
          std::vector<short> next (3584, 5678);
          written = detector.write (reinterpret_cast<char const *> (next.data ()),
                                    next.size () * sizeof (short));
        }
      detector.moveToThread (callerThread);
    })};
    detector.moveToThread (writer.get ());
    writer->start ();
    bool writerReady;
    {
      std::unique_lock<std::mutex> lock {mutex};
      writerReady = changed.wait_for (lock, std::chrono::seconds {5}, [&] { return ready; });
      released = true;
      changed.notify_all ();
    }
    // Both the production capture and the append execute after releasing the
    // gate. No completion handshake orders one sample access before the other.
    Inputs input;
    DecoderProbe probe;
    probe.released = true;
    activeProbe = &probe;
    auto future = input.submit ();
    future.waitForFinished ();
    writer->wait ();
    activeProbe = nullptr;
    QVERIFY (writerReady);
    QVERIFY (writerStarted);
    QCOMPARE (written, qint64 (2 * 3584));
    QCOMPARE (dec_data.params.kin, committed);
    QVERIFY (probe.validCount);
    QCOMPARE (probe.samples.size (), std::size_t (committed));
    QVERIFY (std::all_of (probe.samples.begin (), probe.samples.end (),
                         [] (short x) { return x == 1234; }));
    // Producer appends cannot mutate the decoder's accepted snapshot.
  }

  void overlapping_jobs_retain_submission_identity ()
  {
    std::fill_n (storage.d2, capacity, short (1234));
    std::vector<short> secondSamples (capacity, short (5678));
    Inputs first, second;
    first.arguments[0] = 111000;
    second.arguments[0] = 222000;
    DecoderProbe firstProbe, secondProbe;
    secondProbe.released = true;
    overlappingProbes = {{&firstProbe, &secondProbe}};

    auto firstFuture = first.submit ();
    bool const firstEntered = firstProbe.waitForEntry ();
    // Submission performs the production staging copy synchronously.  The
    // first decoder is already pending but has not consumed any samples.
    auto secondFuture = second.submit (secondSamples.data ());
    firstProbe.release ();
    firstFuture.waitForFinished ();
    secondFuture.waitForFinished ();
    overlappingProbes = {{nullptr, nullptr}};

    QVERIFY (firstEntered);
    QVERIFY (!firstProbe.timedOut);
    QVERIFY (!secondProbe.timedOut);
    QVERIFY (firstProbe.entered);
    QVERIFY (secondProbe.entered);
    QCOMPARE (firstProbe.samples.size (), std::size_t (committed));
    QCOMPARE (secondProbe.samples.size (), std::size_t (committed));
    QVERIFY (std::all_of (firstProbe.samples.begin (), firstProbe.samples.end (),
                         [] (short x) { return x == 1234; }));
    QVERIFY (std::all_of (secondProbe.samples.begin (), secondProbe.samples.end (),
                         [] (short x) { return x == 5678; }));
  }

  void overlapping_submission_does_not_race_decoder ()
  {
    std::fill_n (storage.d2, capacity, short (1234));
    std::vector<short> secondSamples (capacity, short (5678));
    Inputs first, second;
    first.arguments[0] = 111000;
    second.arguments[0] = 222000;
    DecoderProbe firstProbe, secondProbe;
    firstProbe.released = true;
    secondProbe.released = true;
    Rendezvous sampleAccessStart;
    firstProbe.sampleAccessStart = &sampleAccessStart;
    overlappingProbes = {{&firstProbe, &secondProbe}};

    auto firstFuture = first.submit ();
    QFuture<FastDecodeResult> secondFuture;
    std::unique_ptr<QThread> submitter {QThread::create ([&] {
      if (sampleAccessStart.arrive ())
        secondFuture = second.submit (secondSamples.data ());
    })};
    submitter->start ();
    submitter->wait ();
    firstFuture.waitForFinished ();
    secondFuture.waitForFinished ();
    overlappingProbes = {{nullptr, nullptr}};

    QVERIFY (!firstProbe.timedOut);
    QVERIFY (!secondProbe.timedOut);
    QVERIFY (firstProbe.entered);
    QVERIFY (secondProbe.entered);
    // Under TSan, the production staging memcpy conflicts with the first
    // decoder's read.  No data-value assertion relies on that C++ race.
  }
};

QTEST_GUILESS_MAIN (TestFastDecodeOwnership)
#include "test_fast_decode_ownership.moc"
