#include <QtTest>

#include <QAudioDeviceInfo>
#include <QIODevice>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QThread>

#include <atomic>
#include <cmath>
#include <memory>

#if defined (WSJT_ENABLE_NETWORK_AUDIO)
#include "naudio.h"

#include "Audio/NetworkAudioConnection.hpp"
#include "Audio/NetworkSoundOutput.hpp"
#endif

#include "Audio/AudioDevice.hpp"
#include "Audio/AudioSourceRouter.hpp"
#include "Audio/SoundOutputRouter.hpp"
#include "Audio/soundout.h"
#include "Audio/TxPlaybackEvidence.hpp"
#include "Audio/TxAudioQueue.hpp"
#include "Audio/TxRequest.hpp"
#include "Modulator/JttyTxStream.hpp"
#include "DecDataMutex.hpp"
#include "Detector/Detector.hpp"
#include "commons.h"

namespace
{
  dec_data_t test_dec_data {};

  constexpr int native_rate {48000};

  // A modulator stand-in: an AudioDevice whose frames count up from one, so
  // that nothing it produces is silence.
  class RampSource final
    : public AudioDevice
  {
  public:
    explicit RampSource (qint64 frames, AudioDevice::Channel channel = AudioDevice::Mono)
      : m_frames {frames}
    {
      initialize (QIODevice::ReadOnly, channel);
    }
    qint64 served () const {return m_served;}

  protected:
    qint64 readData (char * data, qint64 maxSize) override
    {
      auto const frames = qMin (maxSize / qint64 (bytesPerFrame ()), m_frames - m_served);
      auto * samples = reinterpret_cast<qint16 *> (data);
      for (qint64 i = 0; i < frames; ++i)
        {
          samples = load (static_cast<qint16> (1 + (m_served + i) % 4000), samples);
        }
      m_served += frames;
      return frames * bytesPerFrame ();
    }
    qint64 writeData (char const *, qint64) override {return -1;}

  private:
    qint64 m_frames;
    qint64 m_served {0};
  };

#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  // See test_network_audio_input.cpp: a period long enough that Detector's
  // wall-clock period-wrap reset cannot fire mid-test. Every receive source
  // below runs the application's 4:1 decimation, because a connection opened
  // with its transmit lane subscribes at the server's native 48 kHz.
  constexpr double no_wrap_period {86400.};
  constexpr int native_channels {2};

  std::atomic_bool g_streaming {false};
  std::atomic<long long> g_tx_nonsilent_frames {0};

  void on_stream_started (char const *, void *)
  {
    g_streaming.store (true, std::memory_order_release);
  }

  void on_tx_audio (unsigned char const * pcm, size_t n_bytes, void *)
  {
    auto const * samples = reinterpret_cast<qint16 const *> (pcm);
    auto const frames = n_bytes / (2 * native_channels);
    long long loud {0};
    for (size_t f = 0; f < frames; ++f)
      {
        if (samples[f * native_channels] || samples[f * native_channels + 1]) ++loud;
      }
    g_tx_nonsilent_frames.fetch_add (loud, std::memory_order_acq_rel);
  }

  // An in-process, hardware-free naudio server that also collects the mixed
  // TX audio the NULL backend hands out.
  class TestServer
  {
  public:
    explicit TestServer (int port)
    {
      g_streaming.store (false, std::memory_order_release);
      g_tx_nonsilent_frames.store (0, std::memory_order_release);
      server_ = na_server_create (NA_SERVER_BACKEND_NULL, port);
      if (!server_) return;
      na_server_set_audio_format (server_, native_rate, 16, native_channels);
      na_server_callbacks cbs {};
      cbs.struct_size = sizeof cbs;
      cbs.on_stream_started = &on_stream_started;
      na_server_set_callbacks (server_, &cbs, nullptr);
      na_server_set_tx_audio_cb (server_, &on_tx_audio, nullptr);
      char err[256] {};
      started_ = NA_OK == na_server_start (server_, err, sizeof err);
      error_ = QString::fromUtf8 (err);
    }
    ~TestServer () {if (server_) na_server_destroy (server_);}
    TestServer (TestServer const&) = delete;
    TestServer& operator= (TestServer const&) = delete;

    bool started () const {return started_;}
    QString const& error () const {return error_;}
    QString owner () const
    {
      char buf[128] {};
      auto const n = na_server_tx_owner (server_, buf, sizeof buf);
      return n > 0 ? QString::fromUtf8 (buf, qMin<int> (n, sizeof buf - 1)) : QString {};
    }
    long long nonSilentFrames () const {return g_tx_nonsilent_frames.load (std::memory_order_acquire);}

  private:
    na_audio_server * server_ {nullptr};
    bool started_ {false};
    QString error_;
  };

  int testPort (int offset) {return 45700 + offset;}
#endif

  TxEvidence::TxOutputError fault (QSignalSpy const& spy, int index = 0)
  {
    return qvariant_cast<TxEvidence::TxOutputError> (spy.at (index).first ());
  }

  QString firstError (QSignalSpy const& spy)
  {
    return spy.isEmpty () ? QString {} : fault (spy).message;
  }

  TxEvidence::TxPlaybackContext const context_a {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  TxEvidence::TxPlaybackContext const context_b {TxEvidence::TxSessionId {102}, TxEvidence::TxGeneration {202}};
#endif

  // A restart () queued to `sink`'s thread with no source: every sink refuses
  // it with a fault carrying `session`.
  void restartWithoutSource (AudioOutputSink * sink, qint64 session)
  {
    QMetaObject::invokeMethod (sink, [sink, session] {
      sink->restart (nullptr, {TxEvidence::TxSessionId {session}, TxEvidence::TxGeneration {1}});
    }, Qt::QueuedConnection);
  }
}

dec_data_t& dec_data = test_dec_data;

extern "C" void fil4_state_ (qint16 *, qint32 *, qint16 *, qint32 *, float *)
{
}

class TestSoundOutputRouter
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void initTestCase ()
  {
    qRegisterMetaType<TxEvidence::TxPlaybackContext> ();
    qRegisterMetaType<TxEvidence::TxOutputError> ();
    qRegisterMetaType<TxEvidence::TxRawPlayoutSnapshot> ();
  }

  Q_SLOT void init ()
  {
    std::fill_n (test_dec_data.d2, sizeof test_dec_data.d2 / sizeof *test_dec_data.d2, 0);
    test_dec_data.params = {};
    set_dec_data_input_blocked (false);
  }

  Q_SLOT void defaults_to_the_sound_card ()
  {
    SoundOutputRouter router;
    QVERIFY (router.sink () == SoundOutputRouter::Sink::Device);
    QVERIFY (router.device () != nullptr);
    QCOMPARE (router.active (), router.device ());
    // The network sink exists exactly when the build can use it, and costs
    // nothing until a transmission is routed to it.
    QCOMPARE (router.network () != nullptr, SoundOutputRouter::networkAudioAvailable ());
    QCOMPARE (router.sourceRouter (), static_cast<AudioSourceRouter *> (nullptr));
  }

  // With no output device configured the sound card's SoundOutput reports
  // "No audio output device configured." -- through the router, and also as a
  // DEVICE fault, which is what MainWindow uses to clear the device selection.
  // The device fault comes first: MainWindow's staleness check reads the
  // transmission that the stop requested on error () then ends.
  Q_SLOT void forwards_to_the_sound_card_and_reports_its_faults_as_device_faults ()
  {
    SoundOutputRouter router;
    QSignalSpy errors {&router, &AudioOutputSink::error};
    QSignalSpy device_errors {&router, &SoundOutputRouter::deviceError};
    QSignalSpy snapshots {&router, &AudioOutputSink::rawTxPlayoutSnapshot};
    QStringList order;
    connect (&router, &SoundOutputRouter::deviceError, this, [&order] {order << "deviceError";});
    connect (&router, &AudioOutputSink::error, this, [&order] {order << "error";});

    RampSource ramp {native_rate};
    router.restart (&ramp, context_a);

    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("No audio output device"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).context, context_a);
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (device_errors.count (), 1);
    QCOMPARE (fault (device_errors).context, context_a);
    QCOMPARE (fault (device_errors).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (fault (device_errors).message, fault (errors).message);
    QCOMPARE (order, (QStringList {"deviceError", "error"}));
    QVERIFY (snapshots.count () >= 1);
    QCOMPARE (qvariant_cast<TxEvidence::TxRawPlayoutSnapshot> (snapshots.first ().first ()).context,
              context_a);

    router.stop ();
    QCOMPARE (errors.count (), 1);
    disconnect (&router, nullptr, this, nullptr);
  }

  // MainWindow's output-fault wiring, as the application has it: the sinks on
  // an audio thread, the two handlers on this one. Every sound-card fault --
  // through the router, or from an injected sink -- reaches invalidateDevice
  // before report, whose stop would end the transmission it is judged
  // against; with the network selected no fault reaches invalidateDevice.
  Q_SLOT void output_faults_reach_device_invalidation_before_the_report ()
  {
    QThread audio;
    auto * router = new SoundOutputRouter;
    auto * injected = new SoundOutput;
    router->moveToThread (&audio);
    injected->moveToThread (&audio);
    audio.start ();

    QObject window;
    QStringList calls;
    // On every exit, a failed check included: a running QThread destroyed
    // aborts the whole test binary.
    auto const stop_audio = qScopeGuard ([&] {
      QMetaObject::invokeMethod (router, [router, injected] {
        delete router;
        delete injected;
      }, Qt::BlockingQueuedConnection);
      audio.quit ();
      audio.wait ();
    });
    for (auto * sink : {static_cast<AudioOutputSink *> (router), static_cast<AudioOutputSink *> (injected)})
      {
        SoundOutputRouter::connectOutputFaults (
          sink, &window,
          [&calls] (TxEvidence::TxOutputError const& fault) {
            calls << QString {"invalidate %1"}.arg (fault.context.session_id.value ());
          },
          [&calls] (TxEvidence::TxOutputError const& fault) {
            calls << QString {"report %1"}.arg (fault.context.session_id.value ());
          });
      }

    constexpr int rounds {200};
    QStringList expected;
    for (int session = 1; session <= rounds; ++session)
      {
        restartWithoutSource (session % 2 ? static_cast<AudioOutputSink *> (router) : injected, session);
        expected << QString {"invalidate %1"}.arg (session) << QString {"report %1"}.arg (session);
      }
    QTRY_COMPARE_WITH_TIMEOUT (calls.size (), expected.size (), 10000);
    QCOMPARE (calls, expected);

#if defined (WSJT_ENABLE_NETWORK_AUDIO)
    calls.clear ();
    router->selectSink (SoundOutputRouter::Sink::Network);
    for (int session = 1001; session <= 1000 + rounds; ++session)
      {
        restartWithoutSource (router, session);
      }
    QTRY_COMPARE_WITH_TIMEOUT (calls.size (), rounds, 10000);
    QTest::qWait (100);
    QCOMPARE (calls.size (), rounds);
    for (auto const& call : calls)
      {
        QVERIFY2 (call.startsWith ("report "), qPrintable (call));
      }
#endif
  }

#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  // Selecting the network sink does NOT fall back to the sound card when the
  // network is not ready: the fault is named, and it is not a device fault.
  Q_SLOT void a_network_fault_is_not_a_device_fault ()
  {
    SoundOutputRouter router;
    router.selectSink (SoundOutputRouter::Sink::Network);
    QCOMPARE (router.active (), router.network ());
    QSignalSpy errors {&router, &AudioOutputSink::error};
    QSignalSpy device_errors {&router, &SoundOutputRouter::deviceError};

    RampSource ramp {native_rate};
    router.restart (&ramp, context_b);

    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("no network audio connection"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).context, context_b);
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (device_errors.count (), 0);
    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network);
    QVERIFY (!network->isActive ());
    QCOMPARE (ramp.served (), qint64 {0});
  }

  // The source router's connection is what the network sink transmits on.
  // Opened with its transmit lane (setNetworkTransmit), a restart routed to
  // the network sink reaches the server's TX mixer; the sound card's sink is
  // never restarted.
  Q_SLOT void routes_a_restart_to_the_network_sink_when_selected ()
  {
    TestServer server {testPort (0)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    AudioSourceRouter source_router;
    source_router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (0)));
    source_router.setNetworkTransmit (true);
    source_router.selectSource (AudioSourceRouter::Source::Network);
    QSignalSpy source_errors {&source_router, &AudioInputSource::error};
    source_router.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
    QVERIFY2 (source_errors.isEmpty (), qPrintable (firstError (source_errors)));
    QVERIFY (source_router.networkConnection ());
    QVERIFY (source_router.networkConnection ()->isOpen ());
    QVERIFY (source_router.networkConnection ()->transmitEnabled ());
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    SoundOutputRouter router;
    router.setSourceRouter (&source_router);
    router.selectSink (SoundOutputRouter::Sink::Network);
    QSignalSpy errors {&router, &AudioOutputSink::error};
    QSignalSpy device_errors {&router, &SoundOutputRouter::deviceError};
    QSignalSpy device_snapshots {router.device (), &AudioOutputSink::rawTxPlayoutSnapshot};
    QSignalSpy snapshots {&router, &AudioOutputSink::rawTxPlayoutSnapshot};
    QSignalSpy active {&router, &AudioOutputSink::audioOutputActive};

    RampSource ramp {native_rate};   // one second
    router.restart (&ramp, context_a);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QCOMPARE (active.count (), 1);
    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network && network->isActive ());
    QCOMPARE (network->connection (), source_router.networkConnection ());

    QTRY_VERIFY_WITH_TIMEOUT (server.nonSilentFrames () > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT (!server.owner ().isEmpty (), 5000);
    QTRY_VERIFY_WITH_TIMEOUT (network->isExhausted (), 5000);
    QCOMPARE (network->framesSent (), qint64 {native_rate});

    router.stop ();
    QVERIFY (!network->isActive ());
    QVERIFY (errors.isEmpty ());
    QCOMPARE (device_errors.count (), 0);
    // Nothing reached the sound card's sink: not even the snapshot its own
    // restart () would have published.
    QCOMPARE (device_snapshots.count (), 0);
    // The network sink's snapshots, start to stop, under the restart's context.
    QVERIFY (snapshots.count () >= 2);
    for (auto const& args : snapshots)
      {
        QCOMPARE (qvariant_cast<TxEvidence::TxRawPlayoutSnapshot> (args.first ()).context, context_a);
      }
    // And RX is still open on the shared connection: stopping TX is not
    // closing the link.
    QVERIFY (source_router.networkConnection ()->isOpen ());
    source_router.stop ();
  }

  // A switch made while a transmission runs (Settings accepted during Tune or
  // a timed transmission) leaves that transmission on its route: the running
  // sink keeps playing without a fault, and the next restart () takes the
  // new route, stopping the old sink first.
  Q_SLOT void a_running_transmission_keeps_its_route_across_a_switch ()
  {
    TestServer server {testPort (1)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    AudioSourceRouter source_router;
    source_router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (1)));
    source_router.setNetworkTransmit (true);
    source_router.selectSource (AudioSourceRouter::Source::Network);
    source_router.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    SoundOutputRouter router;
    router.setSourceRouter (&source_router);
    router.selectSink (SoundOutputRouter::Sink::Network);
    QSignalSpy errors {&router, &AudioOutputSink::error};
    QSignalSpy idle {&router, &AudioOutputSink::audioOutputIdle};
    RampSource ramp {10 * native_rate};
    router.restart (&ramp, context_a);
    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network && network->isActive ());
    QTRY_VERIFY_WITH_TIMEOUT (server.nonSilentFrames () > 0, 5000);

    router.selectSink (SoundOutputRouter::Sink::Device);
    QCOMPARE (router.active (), router.device ());
    auto const before = server.nonSilentFrames ();
    QTest::qWait (300);
    QVERIFY (network->isActive ());
    QVERIFY (server.nonSilentFrames () > before);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QCOMPARE (idle.count (), 0);
    QCOMPARE (router.unplayedBufferBytes (), network->unplayedBufferBytes ());

    RampSource next {native_rate};
    router.restart (&next, context_b);
    QVERIFY (!network->isActive ());
    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("No audio output device"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).context, context_b);
    source_router.stop ();
  }

  // Unticking "Use for transmit" (or changing the server) replaces the
  // connection under a running network sink, and the transmission fails Io
  // under its own context -- which stops it -- although the route is switched
  // away from the network sink before the fault arrives.
  Q_SLOT void replacing_the_connection_under_a_running_network_sink_is_an_io_fault ()
  {
    TestServer server {testPort (4)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    AudioSourceRouter source_router;
    source_router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (4)));
    source_router.setNetworkTransmit (true);
    source_router.selectSource (AudioSourceRouter::Source::Network);
    source_router.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    SoundOutputRouter router;
    router.setSourceRouter (&source_router);
    router.selectSink (SoundOutputRouter::Sink::Network);
    QSignalSpy errors {&router, &AudioOutputSink::error};
    QSignalSpy device_errors {&router, &SoundOutputRouter::deviceError};
    RampSource ramp {10 * native_rate};
    router.restart (&ramp, context_a);
    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network && network->isActive ());
    QTRY_VERIFY_WITH_TIMEOUT (server.nonSilentFrames () > 0, 5000);

    // What MainWindow::applyAudioSourceSelection does when the box is unticked.
    source_router.setNetworkTransmit (false);
    router.selectSink (SoundOutputRouter::Sink::Device);
    QTRY_VERIFY_WITH_TIMEOUT (!errors.isEmpty (), 3000);
    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("route changed"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors).context, context_a);
    QCOMPARE (device_errors.count (), 0);
    QVERIFY (!network->isActive ());
    source_router.stop ();
  }

  // Settings accepted with "Use for transmit" unchanged sets the same value
  // again; the connection is kept and the running transmission with it.
  Q_SLOT void setting_network_transmit_unchanged_keeps_a_running_transmission ()
  {
    TestServer server {testPort (5)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    AudioSourceRouter source_router;
    source_router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (5)));
    source_router.setNetworkTransmit (true);
    source_router.selectSource (AudioSourceRouter::Source::Network);
    source_router.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    SoundOutputRouter router;
    router.setSourceRouter (&source_router);
    router.selectSink (SoundOutputRouter::Sink::Network);
    QSignalSpy errors {&router, &AudioOutputSink::error};
    RampSource ramp {10 * native_rate};
    router.restart (&ramp, context_a);
    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network && network->isActive ());
    QTRY_VERIFY_WITH_TIMEOUT (server.nonSilentFrames () > 0, 5000);
    auto * const connection = source_router.networkConnection ();

    source_router.setNetworkTransmit (true);
    router.selectSink (SoundOutputRouter::Sink::Network);
    auto const before = server.nonSilentFrames ();
    QTest::qWait (1500);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QVERIFY (network->isActive ());
    QCOMPARE (source_router.networkConnection (), connection);
    QVERIFY (server.nonSilentFrames () > before);
    router.stop ();
    source_router.stop ();
  }

  // After a Device -> Network switch, stop () and reset () reach the sink that
  // ran -- the sound card, which would otherwise keep playing the modulator
  // after the transmission ends -- and the next restart () takes the network.
  // No device is configured: the refused restart still makes the sound card
  // the running sink, and each sink's snapshots show which one was reached.
  Q_SLOT void stop_reaches_the_sink_that_ran ()
  {
    SoundOutputRouter router;
    RampSource ramp {native_rate};
    router.restart (&ramp, context_a);
    router.selectSink (SoundOutputRouter::Sink::Network);
    QSignalSpy device_snapshots {router.device (), &AudioOutputSink::rawTxPlayoutSnapshot};
    QSignalSpy network_snapshots {router.network (), &AudioOutputSink::rawTxPlayoutSnapshot};
    router.stop ();
    QVERIFY (device_snapshots.count () >= 1);
    QCOMPARE (network_snapshots.count (), 0);

    QSignalSpy errors {&router, &AudioOutputSink::error};
    router.restart (&ramp, context_b);
    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("no network audio connection"), qPrintable (firstError (errors)));
  }

  Q_SLOT void reset_reaches_the_sink_that_ran ()
  {
    SoundOutputRouter router;
    RampSource ramp {native_rate};
    router.restart (&ramp, context_a);
    router.selectSink (SoundOutputRouter::Sink::Network);
    QSignalSpy device_snapshots {router.device (), &AudioOutputSink::rawTxPlayoutSnapshot};
    QSignalSpy network_snapshots {router.network (), &AudioOutputSink::rawTxPlayoutSnapshot};
    router.reset ();
    QVERIFY (device_snapshots.count () >= 1);
    QCOMPARE (network_snapshots.count (), 0);

    QSignalSpy errors {&router, &AudioOutputSink::error};
    router.restart (&ramp, context_b);
    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("no network audio connection"), qPrintable (firstError (errors)));
  }

  // The source router replaces its connection whenever the endpoint changes;
  // the network sink must transmit on the current one, not on the one it saw
  // last time.
  Q_SLOT void the_network_sink_follows_the_source_routers_current_connection ()
  {
    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    AudioSourceRouter source_router;
    source_router.setNetworkTransmit (true);
    source_router.selectSource (AudioSourceRouter::Source::Network);
    SoundOutputRouter router;
    router.setSourceRouter (&source_router);
    router.selectSink (SoundOutputRouter::Sink::Network);
    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network);

    {
      TestServer first {testPort (2)};
      QVERIFY2 (first.started (), qPrintable (first.error ()));
      source_router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (2)));
      source_router.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
      QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
      auto * const first_connection = source_router.networkConnection ();
      RampSource ramp {native_rate / 2};
      router.restart (&ramp, context_a);
      QCOMPARE (network->connection (), first_connection);
      QTRY_VERIFY_WITH_TIMEOUT (first.nonSilentFrames () > 0, 5000);
      router.stop ();
      source_router.stop ();
    }

    TestServer second {testPort (3)};
    QVERIFY2 (second.started (), qPrintable (second.error ()));
    source_router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (3)));
    source_router.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
    QSignalSpy errors {&router, &AudioOutputSink::error};
    RampSource ramp {native_rate / 2};
    router.restart (&ramp, context_a);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QCOMPARE (network->connection (), source_router.networkConnection ());
    QTRY_VERIFY_WITH_TIMEOUT (second.nonSilentFrames () > 0, 5000);
    router.stop ();
    source_router.stop ();
  }
#endif

  Q_SLOT void attenuation_reaches_every_sink ()
  {
    SoundOutputRouter router;
    router.setAttenuation (6.);
    QVERIFY (std::fabs (router.device ()->attenuation () - 6.) < 1e-6);
#if defined (WSJT_ENABLE_NETWORK_AUDIO)
    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network);
    QVERIFY (std::fabs (network->attenuation () - 6.) < 1e-6);
#endif
    router.resetAttenuation ();
    QVERIFY (std::fabs (router.device ()->attenuation ()) < 1e-6);
#if defined (WSJT_ENABLE_NETWORK_AUDIO)
    QVERIFY (std::fabs (network->attenuation ()) < 1e-6);
#endif
  }

#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  // MainWindow moves the router to its audio thread and then selects the
  // sink from the GUI thread; the selection has to be applied on the router's
  // thread, where every sink lives.
  Q_SLOT void a_sink_selected_from_another_thread_is_applied_on_the_routers_thread ()
  {
    QThread worker;
    auto * router = new SoundOutputRouter;
    router->moveToThread (&worker);
    worker.start ();

    router->selectSink (SoundOutputRouter::Sink::Network);

    SoundOutputRouter::Sink sink {SoundOutputRouter::Sink::Device};
    AudioOutputSink * active {nullptr};
    QThread * sink_thread {nullptr};
    QMetaObject::invokeMethod (router, [&] {
      sink = router->sink ();
      active = router->active ();
      sink_thread = active->thread ();
    }, Qt::BlockingQueuedConnection);

    QVERIFY (sink == SoundOutputRouter::Sink::Network);
    QCOMPARE (active, router->network ());
    QCOMPARE (sink_thread, &worker);

    QMetaObject::invokeMethod (router, [router] {delete router;}, Qt::BlockingQueuedConnection);
    worker.quit ();
    worker.wait ();
  }

  // BL-18. JTTY drops PTT on the drained edge with no tail (zeroTailMode), and
  // the edge is trailing silence served past the last real sample reaching the
  // guard JttyTxStream sizes from the sink at start (). On a sound card the
  // device buffer is the whole of what may still be unplayed; on the network
  // path the wire, the server's TX ring and its playback device all lie past
  // the sink, so the guard has to come from unplayedBufferBytes (), not
  // bufferSize (). This drives a real JttyTxStream through the router into the
  // in-process server and checks the edge waited for the declared depth.
  Q_SLOT void jttys_drain_guard_covers_the_network_paths_playout_depth ()
  {
    TestServer server {testPort (6)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    AudioSourceRouter source_router;
    source_router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (6)));
    source_router.setNetworkTransmit (true);
    source_router.selectSource (AudioSourceRouter::Source::Network);
    QSignalSpy source_errors {&source_router, &AudioInputSource::error};
    source_router.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
    QVERIFY2 (source_errors.isEmpty (), qPrintable (firstError (source_errors)));
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    SoundOutputRouter router;
    router.setSourceRouter (&source_router);
    router.selectSink (SoundOutputRouter::Sink::Network);
    QSignalSpy errors {&router, &AudioOutputSink::error};

    auto * network = qobject_cast<NetworkSoundOutput *> (router.network ());
    QVERIFY (network);

    // Half a second of non-silent audio through JTTY's own queue and stream.
    qint64 const real_frames {native_rate / 2};
    TxAudioQueueEpoch const epoch {71};
    TxAudioQueue queue;
    queue.clear (epoch);
    QVector<qint16> audio (static_cast<int> (real_frames));
    for (int i = 0; i < audio.size (); ++i) audio[i] = qint16 (1 + i % 4000);
    QVERIFY (queue.enqueue (audio, epoch).accepted);

    JttyTxStream jtty {queue};
    QSignalSpy drained {&jtty, &JttyTxStream::drained};

    TxEvidence::TxRequest request;
    request.mode = QStringLiteral ("JTTY");
    request.session_id = TxEvidence::TxSessionId {71};
    request.generation = TxEvidence::TxGeneration {1};
    request.queue_epoch = epoch;
    request.channel = AudioDevice::Mono;

    QElapsedTimer since_start;
    since_start.start ();
    jtty.start (request, &router);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QVERIFY (network->isActive ());

    // The edge: every real sample served, and the trailing silence past it has
    // covered the guard.
    QTRY_VERIFY_WITH_TIMEOUT (drained.count () == 1, 15000);
    auto const edge_ms = since_start.elapsed ();
    auto const frames_at_edge = network->framesSent ();
    auto const real_at_server = server.nonSilentFrames ();

    auto const bytes_per_frame = qint64 {2};        // the source is Mono qint16
    auto const declared_frames = network->unplayedBufferBytes () / bytes_per_frame;
    auto const silence_past_audio = frames_at_edge - real_frames;

    // 1. Every real sample was at the server's mixer before PTT could drop.
    QCOMPARE (real_at_server, static_cast<long long> (real_frames));

    // 2. The edge waited for the declared depth, on the sink's own frame clock.
    QVERIFY2 (silence_past_audio >= declared_frames,
              qPrintable (QStringLiteral ("silence past the audio %1 frames < declared depth %2 frames")
                          .arg (silence_past_audio).arg (declared_frames)));

    // 3. The declared depth covers the path past this side, not just this
    //    side's own lead. The floor is the WORST depth measured at the station
    //    (BL-19: 27 tone bursts, the radio's own data VOX as the detector, its
    //    PTT read through rigctld; median 306, p90 425, worst 516). The lead is
    //    inside that figure, since the measurement ran from the client's
    //    inject -- so it is not added again here.
    constexpr qint64 lead_frames {NetworkSoundOutput::leadBlocks * NetworkSoundOutput::blockFrames};
    constexpr qint64 measured_worst_depth_ms {516};
    constexpr qint64 floor_frames {
      measured_worst_depth_ms * NetworkSoundOutput::sampleRate / 1000};
    QVERIFY2 (declared_frames >= floor_frames,
              qPrintable (QStringLiteral ("declared depth %1 frames (%2 ms) is below the worst "
                                          "playout depth measured at the station, %3 frames (%4 ms)")
                          .arg (declared_frames)
                          .arg (declared_frames * 1000 / NetworkSoundOutput::sampleRate)
                          .arg (floor_frames)
                          .arg (floor_frames * 1000 / NetworkSoundOutput::sampleRate)));

    // bufferSize () keeps its own meaning: what THIS side holds, in the wire's
    // frame bytes -- so the two cannot be folded back together unnoticed.
    QCOMPARE (network->bufferSize (),
              int (lead_frames * 2 * native_channels));

    qInfo () << "edge after" << edge_ms << "ms; frames sent" << frames_at_edge
             << "; silence past audio" << silence_past_audio
             << "; declared depth" << declared_frames;

    jtty.stop ();
    router.stop ();
    QVERIFY (errors.isEmpty ());
    source_router.stop ();
  }
#endif
};

QTEST_MAIN (TestSoundOutputRouter)

#include "test_sound_output_router.moc"
