#include <QtTest>

#include <QAudioDeviceInfo>
#include <QSignalSpy>
#include <QThread>

#include <atomic>

#include "naudio.h"

#include "Audio/AudioDevice.hpp"
#include "Audio/AudioSourceRouter.hpp"
#include "Audio/NetworkAudioInput.hpp"
#include "DecDataMutex.hpp"
#include "Detector/Detector.hpp"
#include "commons.h"

namespace
{
  dec_data_t test_dec_data {};

  // See test_network_audio_input.cpp: a period long enough that Detector's
  // wall-clock period-wrap reset cannot fire mid-test.
  constexpr double no_wrap_period {86400.};

  constexpr int native_rate {48000};
  constexpr int native_channels {2};
  constexpr int native_bytes_per_chunk {native_rate * 20 / 1000 * native_channels * 2};
  constexpr int granted_frames_per_chunk {NetworkAudioInput::detectorSampleRate * 20 / 1000};

  std::atomic_bool g_streaming {false};

  void on_stream_started (char const *, void *)
  {
    g_streaming.store (true, std::memory_order_release);
  }

  class TestServer
  {
  public:
    explicit TestServer (int port)
    {
      g_streaming.store (false, std::memory_order_release);
      server_ = na_server_create (NA_SERVER_BACKEND_NULL, port);
      if (!server_) return;
      na_server_set_audio_format (server_, native_rate, 16, native_channels);
      na_server_callbacks cbs {};
      cbs.struct_size = sizeof cbs;
      cbs.on_stream_started = &on_stream_started;
      na_server_set_callbacks (server_, &cbs, nullptr);
      char err[256] {};
      started_ = NA_OK == na_server_start (server_, err, sizeof err);
      error_ = QString::fromUtf8 (err);
    }
    ~TestServer () {if (server_) na_server_destroy (server_);}
    TestServer (TestServer const&) = delete;
    TestServer& operator= (TestServer const&) = delete;

    bool started () const {return started_;}
    QString const& error () const {return error_;}
    void injectChunk ()
    {
      QByteArray pcm (native_bytes_per_chunk, '\0');
      na_server_inject_audio (
        server_, reinterpret_cast<unsigned char const *> (pcm.constData ()),
        pcm.size ());
    }

  private:
    na_audio_server * server_ {nullptr};
    bool started_ {false};
    QString error_;
  };

  int testPort (int offset) {return 45500 + offset;}

  // Detector stages audio in a receive buffer of its own and reports the
  // period's running frame count through framesWritten (), once per block.
  // With the block sized to one chunk that count is the arrival tally this
  // case asserts on; nothing else the test can reach carries it.
  qint64 lastFramesWritten (QSignalSpy const& written)
  {
    return written.isEmpty () ? qint64 {0}
                              : written.constLast ().constFirst ().toLongLong ();
  }
}

dec_data_t& dec_data = test_dec_data;

extern "C" void fil4_state_ (qint16 *, qint32 *, qint16 *, qint32 *, float *)
{
}

class TestAudioSourceRouter
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void init ()
  {
    std::fill_n (test_dec_data.d2, sizeof test_dec_data.d2 / sizeof *test_dec_data.d2, 0);
    test_dec_data.params = {};
    set_dec_data_input_blocked (false);
  }

  Q_SLOT void defaults_to_the_sound_card ()
  {
    AudioSourceRouter router;
    QVERIFY (router.source () == AudioSourceRouter::Source::Device);
    QVERIFY (router.active () != nullptr);
    // An untouched installation must behave exactly as it did, so the default
    // has to be the device with no network object built at all.
    QVERIFY (router.networkHost ().isEmpty ());
  }

  Q_SLOT void selecting_network_without_a_server_stays_on_the_sound_card ()
  {
    AudioSourceRouter router;
    auto * device = router.active ();

    router.selectSource (AudioSourceRouter::Source::Network);

    // The selection is recorded, but with nowhere to connect the router must
    // not leave the station deaf.
    QVERIFY (router.source () == AudioSourceRouter::Source::Network);
    QCOMPARE (router.active (), device);
  }

  Q_SLOT void routes_to_the_network_source_when_configured ()
  {
    if (!AudioSourceRouter::networkAudioAvailable ()) QSKIP ("built without network audio");

    AudioSourceRouter router;
    auto * device = router.active ();
    router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (0)));
    router.selectSource (AudioSourceRouter::Source::Network);

    QVERIFY (router.active () != device);
    QCOMPARE (router.networkHost (), QString {"127.0.0.1"});
    QCOMPARE (router.networkPort (), static_cast<quint16> (testPort (0)));
  }

  Q_SLOT void delegates_start_resume_and_audio_to_the_network_source ()
  {
    if (!AudioSourceRouter::networkAudioAvailable ()) QSKIP ("built without network audio");

    TestServer server {testPort (1)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    detector.setBlockSize (granted_frames_per_chunk);

    AudioSourceRouter router;
    QSignalSpy errors {&router, &AudioInputSource::error};
    QSignalSpy descriptors {&router, &AudioInputSource::streamDescriptorChanged};
    QSignalSpy written {&detector, &Detector::framesWritten};
    router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (1)));
    router.selectSource (AudioSourceRouter::Source::Network);

    router.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);
    QVERIFY (errors.isEmpty ());

    // The child's descriptor is republished through the router, so callers
    // that watch the router alone -- which is all of MainWindow -- see it.
    QCOMPARE (descriptors.count (), 1);
    QCOMPARE (router.streamDescriptor ().sample_rate_hz,
              NetworkAudioInput::detectorSampleRate);

    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
    router.resume ();
    server.injectChunk ();
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written),
                               qint64 {granted_frames_per_chunk}, 5000);

    router.suspend ();
    auto const after_suspend = written.count ();
    server.injectChunk ();
    QTest::qWait (400);
    QCOMPARE (written.count (), after_suspend);

    router.stop ();
  }

  Q_SLOT void switching_away_while_running_stops_the_network_source ()
  {
    if (!AudioSourceRouter::networkAudioAvailable ()) QSKIP ("built without network audio");

    TestServer server {testPort (2)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    AudioSourceRouter router;
    router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (2)));
    router.selectSource (AudioSourceRouter::Source::Network);
    router.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);

    auto * network = qobject_cast<NetworkAudioInput *> (router.active ());
    QVERIFY (network);
    QVERIFY (network->isConnected ());

    // Switching back mid-run must release the stream rather than leave a
    // socket open feeding nothing.
    router.selectSource (AudioSourceRouter::Source::Device);
    QVERIFY (!network->isConnected ());
    QVERIFY (router.active () != network);
    // And what the router publishes must be the new source's state -- here
    // a sound card that was given no device and so is not streaming -- not
    // the last thing the old source said.
    QVERIFY (!router.active ()->streamDescriptor ().isValid ());
    QVERIFY (router.streamDescriptor () == router.active ()->streamDescriptor ());
  }

  // The switch back is the transition the cases above cannot see: they start
  // the router with a null device, and a sound card that was never running
  // has nothing to lose. Started on a real device, as MainWindow does, the
  // sound card must come back when the selection returns to it. This is the
  // ordinary way out of an opt-in feature, and nothing else restarts it:
  // unchecking Network audio with the device unchanged is no
  // restart_audio_input () in MainWindow's eyes.
  Q_SLOT void switching_back_restarts_the_sound_card ()
  {
    if (!AudioSourceRouter::networkAudioAvailable ()) QSKIP ("built without network audio");
    auto const device = QAudioDeviceInfo::defaultInputDevice ();
    if (device.isNull ()) QSKIP ("no default input device on this host");

    TestServer server {testPort (5)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    AudioSourceRouter router;
    QSignalSpy errors {&router, &AudioInputSource::error};

    router.start (device, 0, &detector, 4, AudioDevice::Mono);
    if (!errors.isEmpty ())
      {
        // A host whose default device will not open at the decoder rate
        // cannot establish the premise; that is not the router's doing.
        auto const why = ("the default input device did not start: "
                          + errors.first ().first ().toString ()).toLocal8Bit ();
        QSKIP (why.constData ());
      }
    QVERIFY (router.streamDescriptor ().isValid ());

    router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (5)));
    router.selectSource (AudioSourceRouter::Source::Network);
    QVERIFY (qobject_cast<NetworkAudioInput *> (router.active ()));

    router.selectSource (AudioSourceRouter::Source::Device);
    QVERIFY (!qobject_cast<NetworkAudioInput *> (router.active ()));
    // Ask the sound card itself, not the router's copy of its descriptor.
    QVERIFY2 (router.active ()->streamDescriptor ().isValid (),
              "the sound card did not restart after the switch back");
    QVERIFY (router.streamDescriptor () == router.active ()->streamDescriptor ());
    QVERIFY (errors.isEmpty ());

    router.stop ();
  }

  Q_SLOT void stop_releases_every_source ()
  {
    if (!AudioSourceRouter::networkAudioAvailable ()) QSKIP ("built without network audio");

    TestServer server {testPort (3)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    AudioSourceRouter router;
    router.setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (3)));
    router.selectSource (AudioSourceRouter::Source::Network);
    router.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);

    auto * network = qobject_cast<NetworkAudioInput *> (router.active ());
    QVERIFY (network && network->isConnected ());

    router.stop ();

    QVERIFY (!network->isConnected ());
    QVERIFY (!router.streamDescriptor ().isValid ());
  }

  // MainWindow moves the router to its audio thread and then configures it
  // from the GUI thread. The network source it builds must live on the
  // router's thread whatever thread asked for it: a QObject only takes its
  // parent's thread when it is constructed ON that thread, and one built from
  // elsewhere is refused as a child and left on the caller's thread -- where
  // every queued slot of its marshalling design then runs, and the Detector is
  // written from the wrong thread. The application's console cannot be relied
  // on to show this (WSJT-X's Qt message handler drops warnings), so the
  // affinity is asserted directly.
  Q_SLOT void a_source_configured_from_another_thread_lives_on_the_routers_thread ()
  {
    if (!AudioSourceRouter::networkAudioAvailable ()) QSKIP ("built without network audio");

    QThread worker;
    auto * router = new AudioSourceRouter;
    router->moveToThread (&worker);
    worker.start ();

    // From this thread, exactly as MainWindow does from the GUI thread.
    router->setNetworkServer ("127.0.0.1", static_cast<quint16> (testPort (4)));
    router->selectSource (AudioSourceRouter::Source::Network);

    // A blocking call posted after those two runs after them on the worker,
    // so what it reads is the state they left behind.
    AudioInputSource * network {nullptr};
    AudioSourceRouter::Source source {AudioSourceRouter::Source::Device};
    QMetaObject::invokeMethod (router, [&] {
      source = router->source ();
      network = router->active ();
    }, Qt::BlockingQueuedConnection);

    QVERIFY (source == AudioSourceRouter::Source::Network);
    QVERIFY (network);
    QVERIFY (qobject_cast<NetworkAudioInput *> (network));
    QCOMPARE (network->thread (), &worker);
    QCOMPARE (network->parent (), router);

    QMetaObject::invokeMethod (router, [router] {delete router;}, Qt::BlockingQueuedConnection);
    worker.quit ();
    worker.wait ();
  }
};

QTEST_MAIN (TestAudioSourceRouter)

#include "test_audio_source_router.moc"
