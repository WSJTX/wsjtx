#include <QtTest>
#include <QMutexLocker>
#include <QThread>
#include <boost/log/keywords/channel.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "DecDataMutex.hpp"
#include "FastDecode.hpp"
#include "TciSimScript.hpp"
#include "TciSimServer.hpp"
#include "Transceiver/TCITransceiver.hpp"
#include "widgets/itoneAndicw.h"
#include "wsjtx_config.h"

namespace
{
dec_data_t storage {};
constexpr int block = 3456;
constexpr int captureCount = block;

struct ReceiveClock
{
  std::mutex mutex;
  std::condition_variable changed;
  qint64 timestamp = 104000; // 14 s into a 15 s period; 105000 wraps to zero.
  bool armed = false, paused = false, released = false, timedOut = false;

  qint64 now ()
  {
    std::unique_lock<std::mutex> lock {mutex};
    if (armed)
      {
        armed = false;
        paused = true;
        changed.notify_all ();
        timedOut = !changed.wait_for (lock, std::chrono::seconds {5}, [&] { return released; });
      }
    return timestamp;
  }
  void set (qint64 value) { std::lock_guard<std::mutex> lock {mutex}; timestamp = value; }
  void arm () { std::lock_guard<std::mutex> lock {mutex}; armed = true; }
  bool waitPaused ()
  {
    std::unique_lock<std::mutex> lock {mutex};
    return changed.wait_for (lock, std::chrono::seconds {5}, [&] { return paused; });
  }
  void release ()
  {
    std::lock_guard<std::mutex> lock {mutex};
    released = true;
    changed.notify_all ();
  }
};

// The real decoder launcher is linked; only the downstream decoder is a probe.
struct DecodeProbe
{
  std::mutex mutex;
  std::condition_variable changed;
  bool released = false, timedOut = false;
  std::vector<short> samples;
  int frames = 0;
  double period = 0;
  QByteArray myCall, hisCall;
  void release ()
  {
    std::lock_guard<std::mutex> lock {mutex};
    released = true;
    changed.notify_all ();
  }
};
DecodeProbe * activeProbe = nullptr;

struct Job
{
  std::array<int, 15> arguments {};
  std::array<char, 8000> messages {};
  char myCall[12], hisCall[12];
  double period = 15;
  Job ()
  {
    arguments[1] = captureCount;
    arguments[3] = 1;
    arguments[9] = 104;
    std::memcpy (myCall, "K1ABC       ", 12);
    std::memcpy (hisCall, "W9XYZ       ", 12);
  }
  QFuture<FastDecodeResult> submit (short const * samples = storage.d2)
  {
    return startFastDecode (samples, arguments.data (), period,
                            myCall, hisCall);
  }
};

class Harness
{
public:
  Harness () : logger_ {boost::log::keywords::channel = "TCI_RECEIVE_TEST"}
  {
    QObject::connect (&server, &TciSimServer::client_arrived, &server, [this] (int) {
      server.send_text (TciSimScript::greeting_messages (TciSimGreeting::Thetis).join (';'));
    });
    QObject::connect (&server, &TciSimServer::text_received, &server,
                      [this] (QString const& text, int) {
      for (auto const& command : text.split (';', Qt::SkipEmptyParts))
        {
          auto const verb = command.section (':', 0, 0);
          if (verb == "split_enable") server.send_text ("split_enable:0,false");
          else if (verb == "audio_start" || verb == "audio_stop"
                   || verb == "modulation" || verb == "rx_enable"
                   || verb == "trx" || verb == "start" || verb == "stop")
            server.send_text (command);
          else if (verb == "vfo")
            server.send_text (command.count (',') >= 2 ? command : "vfo:0,0,14074000");
        }
    });
    context_ = new QObject;
    context_->moveToThread (&thread_);
    QObject::connect (&thread_, &QThread::finished, context_, &QObject::deleteLater);
    thread_.start ();
  }

  ~Harness ()
  {
    clock.release ();
    auto stopped = std::make_shared<std::atomic_bool> (false);
    QMetaObject::invokeMethod (context_, [this, stopped] {
      if (rig) { rig->stop (); delete rig; rig = nullptr; }
      stopped->store (true);
    }, Qt::QueuedConnection);
    server.wait_for ([&] { return stopped->load (); }, 10000);
    // Complete destruction before logger, clock and transport storage expire.
    thread_.quit ();
    thread_.wait ();
    server.close ();
  }

  bool start ()
  {
    if (!server.listen ()) return false;
    QString const address = QStringLiteral ("127.0.0.1:%1").arg (server.port ());
    QMetaObject::invokeMethod (context_, [this, address] {
      rig = new TCITransceiver (&logger_, {}, "0", address, true, tci__audio,
                                context_, [this] { return clock.now (); });
      QObject::connect (rig, &Transceiver::failure, rig, [this] (QString const&) {
        failed_.store (true);
      });
      QObject::connect (rig, &Transceiver::tciframeswritten, rig, [this] (qint64) {
        ++notifications_;
      });
      QObject::connect (rig, &Transceiver::receiveAudio, &audioContext_,
                        [this] (ReceiveAudio audio) {
        receiver_.accept (audio, storage);
      }, Qt::QueuedConnection);
      // A ready message is an ordered fence behind preceding binary frames.
      QObject::connect (rig, &TCITransceiver::tci_done7, rig, [this] {
        std::lock_guard<std::mutex> lock {fenceMutex_};
        ++fences_;
        fenceChanged_.notify_all ();
      });
      rig->start (1);
      state_.online (true);
      state_.frequency (14074000);
      state_.mode (Transceiver::USB);
      state_.period (15);
      state_.audio (true);
      state_.volume (0);
      rig->set (state_, 2);
      started_.store (true);
    }, Qt::QueuedConnection);
    return server.wait_for ([this] { return started_.load (); }, 10000)
      && !failed_.load () && server.client_connected ();
  }

  bool enableAudio (bool enabled)
  {
    QMetaObject::invokeMethod (context_, [this, enabled] {
      state_.audio (enabled);
      rig->set (state_, ++sequence_);
    }, Qt::BlockingQueuedConnection);
    return !failed_.load ();
  }

  bool fence ()
  {
    std::unique_lock<std::mutex> lock {fenceMutex_};
    auto const before = fences_;
    server.send_text ("ready");
    server.flush ();
    // No GUI event dispatch here: queued audio notifications may stay pending.
    return fenceChanged_.wait_for (lock, std::chrono::seconds {5},
                                   [&] { return fences_ > before; });
  }

  bool packet (std::vector<float> const& pcm, int offset, int count, int receiver = 0)
  {
    QByteArray bytes;
    if (!TciStream::prepare_float_frame (&bytes, 2 * count)) return false;
    auto * header = TciStream::header (&bytes);
    header->receiver = receiver;
    header->sampleRate = 48000;
    header->format = TciStream::Float32Format;
    header->type = TciStream::RxAudioStream;
    header->length = 2 * count;
    header->channels = 2;
    auto * payload = TciStream::float_payload (&bytes);
    for (int i = 0; i < count; ++i)
      {
        payload[2 * i] = pcm[offset + i];
        payload[2 * i + 1] = -pcm[offset + i]; // Distinct unused right channel.
      }
    bool const sent = server.send_binary (bytes);
    return sent;
  }

  bool feed (std::vector<float> const& pcm, std::vector<int> const& chunks = {4096},
             int receiver = 0)
  {
    int offset = 0, index = 0;
    while (offset < int (pcm.size ()))
      {
        int count = std::min (chunks[index++ % chunks.size ()], int (pcm.size ()) - offset);
        if (!packet (pcm, offset, count, receiver)) return false;
        offset += count;
      }
    return fence ();
  }

  std::vector<short> snapshot ()
  {
    QCoreApplication::sendPostedEvents (&audioContext_, QEvent::MetaCall);
    QMutexLocker lock {&dec_data_mutex ()};
    return {storage.d2, storage.d2 + storage.params.kin};
  }
  int notifications () const { return notifications_.load (); }

  TciSimServer server;
  ReceiveClock clock;
  TCITransceiver * rig = nullptr;
private:
  Transceiver::logger_type logger_;
  QThread thread_;
  QObject * context_ = nullptr;
  QObject audioContext_;
  ReceiveAudioConsumer receiver_;
  Transceiver::TransceiverState state_;
  unsigned sequence_ = 2;
  std::atomic_bool started_ {false}, failed_ {false};
  std::atomic_int notifications_ {0};
  std::mutex fenceMutex_;
  std::condition_variable fenceChanged_;
  int fences_ = 0;
};

std::vector<float> signal (int outputFrames, float value)
{
  return std::vector<float> (4 * outputFrames, value);
}
}

dec_data_t& dec_data = storage;
int volatile itone[MAX_NUM_SYMBOLS] {};
int volatile icw[NUM_CW_SYMBOLS] {};
float gran () { return 0; }

extern "C" void fast_decode_ (short samples[], int arguments[], double * period,
                               char[], char myCall[], char hisCall[],
                               fortran_charlen_t, fortran_charlen_t, fortran_charlen_t)
{
  auto& probe = *activeProbe;
  std::unique_lock<std::mutex> lock {probe.mutex};
  probe.timedOut = !probe.changed.wait_for (lock, std::chrono::seconds {5},
                                           [&] { return probe.released; });
  if (probe.timedOut) return;
  probe.frames = arguments[1];
  if (probe.frames >= 0 && probe.frames <= 360000)
    probe.samples.assign (samples, samples + probe.frames);
  probe.period = *period;
  probe.myCall = QByteArray (myCall, 12);
  probe.hisCall = QByteArray (hisCall, 12);
}

class TestTciReceiveOwnership : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void packetization_preserves_audio ()
  {
    Harness h;
    QVERIFY (h.start ());
    auto pcm = signal (captureCount, 0.0f);
    for (std::size_t i = 0; i < pcm.size (); ++i)
      pcm[i] = (int (i % 31) - 15) / 32.0f;
    // fil4 has persistent FIR history. Feed the same silent pre-roll for each
    // run; do not reset it through test-only access to Fortran saved state.
    QVERIFY (h.feed (signal (block, 0.0f)));
    QVERIFY (h.feed (pcm));
    auto first = h.snapshot ();
    QVERIFY (h.enableAudio (false));
    QVERIFY (h.enableAudio (true));
    QVERIFY (h.feed (signal (block, 0.0f), {4095}));
    QVERIFY (h.feed (pcm, {4095}));
    auto second = h.snapshot ();
    QCOMPARE (first.size (), std::size_t (2 * block));
    QCOMPARE (second.size (), first.size ());
    QVERIFY (std::any_of (first.begin () + block, first.end (), [] (short x) { return x != 0; }));
    // Discard pre-roll output: its first taps legitimately contain history
    // from the preceding stream. Compare only identically conditioned audio.
    QVERIFY (std::equal (first.begin () + block, first.end (), second.begin () + block));
  }

  void ignored_audio_does_not_change_reception ()
  {
    Harness h;
    QVERIFY (h.start ());
    QVERIFY (h.feed (signal (captureCount, 0.125f)));
    auto before = h.snapshot ();
    int const notifications = h.notifications ();

    QVERIFY (h.feed (signal (block, -0.25f), {4096}, 1));
    QVERIFY (before == h.snapshot ());
    QCOMPARE (h.notifications (), notifications);

    QVERIFY (h.enableAudio (false));
    QVERIFY (h.feed (signal (block, -0.25f)));
    QVERIFY (before == h.snapshot ());
    QCOMPARE (h.notifications (), notifications);
    // An accepted block is a positive control proving the transport and gate
    // work, rather than mistaking an idle or disconnected receiver for success.
    QVERIFY (h.enableAudio (true));
    QVERIFY (h.feed (signal (block, -0.25f)));
    QCOMPARE (h.snapshot ().size (), std::size_t (block));
  }

  void submitted_job_survives_period_reuse ()
  {
    Harness h;
    QVERIFY (h.start ());
    QVERIFY (h.feed (signal (captureCount, 0.125f)));
    auto const expected = h.snapshot ();
    Job job;
    DecodeProbe probe;
    activeProbe = &probe;
    auto future = job.submit ();
    h.clock.set (105000);
    bool const fed = h.feed (signal (captureCount, -0.25f));
    auto const replacement = h.snapshot ();
    probe.release ();
    future.waitForFinished ();
    activeProbe = nullptr;
    QVERIFY (fed);
    QVERIFY (!probe.timedOut);
    QCOMPARE (replacement.size (), expected.size ());
    QVERIFY (replacement != expected);
    QCOMPARE (probe.samples.size (), expected.size ());
    QCOMPARE (probe.samples.back (), expected.back ());
    QVERIFY (probe.samples == expected);
    QCOMPARE (probe.frames, captureCount);
    QCOMPARE (probe.period, 15.0);
    QCOMPARE (probe.myCall, QByteArray ("K1ABC       ", 12));
    QCOMPARE (probe.hisCall, QByteArray ("W9XYZ       ", 12));
  }

  void capture_vs_transport_append ()
  {
    Harness h;
    QVERIFY (h.start ());
    QVERIFY (h.feed (signal (captureCount, 0.125f)));
    auto const expected = h.snapshot ();
    // Leave one stereo frame before the next real downsampler output block.
    auto next = signal (block, -0.25f);
    next.pop_back ();
    QVERIFY (h.feed (next));
    h.clock.arm ();
    QVERIFY (h.packet (std::vector<float> { -0.25f }, 0, 1));
    h.server.flush ();
    bool const paused = h.clock.waitPaused ();
    Job job;
    DecodeProbe probe;
    probe.released = true;
    activeProbe = &probe;
    h.clock.release ();
    // Both real sample accesses follow release, with no completion ordering.
    auto future = job.submit ();
    bool const finished = h.fence ();
    future.waitForFinished ();
    activeProbe = nullptr;
    QVERIFY (paused);
    QVERIFY (finished);
    QVERIFY (!h.clock.timedOut);
    QVERIFY (!probe.timedOut);
    QCOMPARE (h.snapshot ().size (), std::size_t (captureCount + block));
    QVERIFY (probe.samples == expected);
  }

  void delayed_notification_retains_period_identity ()
  {
    Harness h;
    QVERIFY (h.start ());
    Job job;
    DecodeProbe probe;
    probe.released = true;
    activeProbe = &probe;
    QObject consumer;
    bool handled = false;
    int rejected = 0;
    auto received = std::make_unique<dec_data_t> ();
    ReceiveAudioConsumer receiver;
    QFuture<FastDecodeResult> future;
    QObject::connect (h.rig, &Transceiver::receiveAudio, &consumer, [&] (ReceiveAudio audio) {
      if (!receiver.accept (audio, *received)) { ++rejected; return; }
      if (handled || audio->end () != captureCount) return;
      handled = true;
      future = job.submit (received->d2);
    }, Qt::QueuedConnection);
    // Transport fences do not dispatch the main thread's queued callbacks.
    bool const fedA = h.feed (signal (captureCount, 0.125f));
    auto const expected = h.snapshot ();
    h.clock.set (105000);
    bool const fedB = h.feed (signal (captureCount, -0.25f));
    auto const replacement = h.snapshot ();
    QCoreApplication::sendPostedEvents (&consumer, QEvent::MetaCall);
    future.waitForFinished ();
    activeProbe = nullptr;
    QVERIFY (fedA);
    QVERIFY (fedB);
    QVERIFY (handled);
    QVERIFY (rejected > 0);
    QVERIFY (!probe.timedOut);
    QCOMPARE (expected.size (), std::size_t (captureCount));
    QCOMPARE (replacement.size (), expected.size ());
    QVERIFY (replacement != expected);
    QCOMPARE (probe.samples.size (), expected.size ());
    QCOMPARE (probe.samples.back (), replacement.back ());
    QVERIFY (probe.samples == replacement);
    // Stale A is rejected; B still reaches the production decode boundary.
  }
};

QTEST_GUILESS_MAIN (TestTciReceiveOwnership)
#include "test_tci_receive_ownership.moc"
