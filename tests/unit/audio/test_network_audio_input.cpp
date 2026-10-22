#include <QtTest>

#include <QAudioDeviceInfo>
#include <QByteArray>
#include <QSignalSpy>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>

#include "naudio.h"

#include "Audio/AudioDevice.hpp"
#include "Audio/AudioStreamDescriptor.hpp"
#include "Audio/NetworkAudioConnection.hpp"
#include "Audio/NetworkAudioInput.hpp"
#include "DecDataMutex.hpp"
#include "Detector/Detector.hpp"
#include "commons.h"

namespace
{
  dec_data_t test_dec_data {};

  void clear_dec_data (dec_data_t& data)
  {
    std::fill_n (data.d2, sizeof data.d2 / sizeof *data.d2, 0);
    data.params = {};
  }

  // A period long enough that Detector's wall-clock period-wrap reset cannot
  // fire during a test. Detector zeroes the buffer when the millisecond offset
  // into the period goes backwards; with a one-second period that happens once
  // a second and would make every assertion below a coin toss.
  constexpr double no_wrap_period {86400.};

  // One 20 ms frame of the server's native 48 kHz stereo audio.
  constexpr int native_rate {48000};
  constexpr int native_channels {2};
  constexpr int frame_ms {20};
  constexpr int native_frames_per_chunk {native_rate * frame_ms / 1000};      // 960

  // What the same 20 ms becomes once the server decimates it to the format
  // NetworkAudioInput asks for: 12 kHz mono.
  constexpr int granted_frames_per_chunk {NetworkAudioInput::detectorSampleRate * frame_ms / 1000};

  QByteArray nativeChunk (int seed, int channels = native_channels)
  {
    QByteArray chunk (native_frames_per_chunk * channels * 2, '\0');
    auto * samples = reinterpret_cast<qint16 *> (chunk.data ());
    for (int frame = 0; frame < native_frames_per_chunk; ++frame)
      {
        auto const value = static_cast<qint16> ((seed + frame) % 4096);
        for (int ch = 0; ch < channels; ++ch)
          {
            samples[frame * channels + ch] = value;
          }
      }
    return chunk;
  }

  std::atomic_bool g_streaming {false};

  void on_stream_started (char const *, void *)
  {
    g_streaming.store (true, std::memory_order_release);
  }

  void on_stream_stopped (char const *, void *)
  {
    g_streaming.store (false, std::memory_order_release);
  }

  // Owns an in-process, hardware-free naudio server. RX audio comes in through
  // na_server_inject_audio, so the test needs no sound card, no radio and no
  // external process.
  class TestServer
  {
  public:
    explicit TestServer (int port, int rate = native_rate,
                         int channels = native_channels)
    {
      g_streaming.store (false, std::memory_order_release);
      server_ = na_server_create (NA_SERVER_BACKEND_NULL, port);
      if (!server_) return;
      na_server_set_audio_format (server_, rate, 16, channels);

      na_server_callbacks cbs {};
      cbs.struct_size = sizeof cbs;
      cbs.on_stream_started = &on_stream_started;
      cbs.on_stream_stopped = &on_stream_stopped;
      na_server_set_callbacks (server_, &cbs, nullptr);

      char err[256] {};
      started_ = NA_OK == na_server_start (server_, err, sizeof err);
      error_ = QString::fromUtf8 (err);
    }

    ~TestServer ()
    {
      if (server_) na_server_destroy (server_);
    }

    TestServer (TestServer const&) = delete;
    TestServer& operator= (TestServer const&) = delete;

    bool started () const {return started_;}
    QString const& error () const {return error_;}

    void inject (QByteArray const& pcm)
    {
      na_server_inject_audio (
        server_, reinterpret_cast<unsigned char const *> (pcm.constData ()),
        pcm.size ());
    }

  private:
    na_audio_server * server_ {nullptr};
    bool started_ {false};
    QString error_;
  };

  // An OS-assigned port would be ideal, but the client has to be told where to
  // connect before the server can report one, so pick from the ephemeral range
  // and let a bind clash fail loudly rather than silently reusing a live port.
  int testPort (int offset) {return 45400 + offset;}

  // Detector stages audio in a receive buffer of its own and reports the
  // period's running frame count through framesWritten (), once per block.
  // With the block sized to one chunk that count is the arrival tally these
  // cases assert on; nothing else the tests can reach carries it.
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

class TestNetworkAudioInput
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void init ()
  {
    clear_dec_data (test_dec_data);
    set_dec_data_input_blocked (false);
  }

  Q_SLOT void cleanup ()
  {
    set_dec_data_input_blocked (false);
  }

  Q_SLOT void requests_and_receives_the_decoder_format ()
  {
    TestServer server {testPort (0)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (0))};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy descriptors {&source, &AudioInputSource::streamDescriptorChanged};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);
    QVERIFY2 (errors.isEmpty (),
              qPrintable (errors.isEmpty () ? QString {}
                                            : errors.first ().first ().toString ()));
    QVERIFY (source.isConnected ());

    // The server is 48 kHz stereo and the source asked for the RATE only, so
    // 12 kHz arrives still stereo. Asking for a channel layout as well would
    // decline the whole request on a mono server and lose the rate with it,
    // so the channel is chosen here rather than on the wire.
    QCOMPARE (descriptors.count (), 1);
    auto const descriptor = source.streamDescriptor ();
    QCOMPARE (descriptor.sample_rate_hz, NetworkAudioInput::detectorSampleRate);
    QCOMPARE (descriptor.channel_count, 2);
    QCOMPARE (descriptor.sample_size_bits, 16);
    QVERIFY (descriptor.sample_encoding
             == AudioStreamDescriptor::SampleEncoding::SignedInteger);
    QVERIFY (descriptor.byte_order == AudioStreamDescriptor::ByteOrder::LittleEndian);
    // Network audio is not this host's device clock, and net-audio carries no
    // capture anchor, so alignment falls back to arrival time by design.
    QVERIFY (descriptor.clock_domain == AudioStreamDescriptor::ClockDomain::SystemClock);
    QVERIFY (descriptor.timing_evidence
             == AudioStreamDescriptor::TimingEvidence::Unavailable);
    QCOMPARE (descriptor.capture_anchor_utc_ms, qint64 {0});
    // A4: discontinuities ARE surfaced -- lifecycle events for an outage,
    // transport counters for loss while the link holds -- so claiming the
    // capability is now true rather than a lie to AudioStreamClock's consumers.
    QVERIFY (descriptor.can_report_discontinuities);
    // Detection is not clock discipline: with no capture anchor on the wire the
    // timing evidence stays Unavailable, and skew is uncorrected by design.
    QVERIFY (descriptor.timing_evidence
             == AudioStreamDescriptor::TimingEvidence::Unavailable);

    source.stop ();
  }

  Q_SLOT void feeds_dec_data_through_the_detector ()
  {
    TestServer server {testPort (1)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    // Detector emits framesWritten once per FFT block, and its default block is
    // larger than this test's whole payload. Size the block to one 20 ms chunk
    // so the signal that drives dataSink () -- and so the waterfall and the
    // decode triggers -- is exercised rather than merely assumed. MainWindow
    // does the same thing through its FFTSize signal.
    detector.setBlockSize (granted_frames_per_chunk);
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (1))};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy written {&detector, &Detector::framesWritten};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);
    QVERIFY (errors.isEmpty ());
    QVERIFY (source.isConnected ());

    // Wait for the server to see this client actually streaming: audio injected
    // before that is broadcast to nobody.
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    source.resume ();

    constexpr int chunks {5};
    for (int i = 0; i < chunks; ++i)
      {
        server.inject (nativeChunk (i * 97));
      }

    // 20 ms of 48 kHz stereo in becomes 20 ms of 12 kHz mono out, per chunk.
    qint64 const expected {qint64 {chunks} * granted_frames_per_chunk};
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written), expected, 5000);
    // One block per chunk, so the downstream signal count is exact.
    QCOMPARE (written.count (), chunks);
    QVERIFY (errors.isEmpty ());

    source.stop ();
    QVERIFY (!source.isConnected ());
  }

  Q_SLOT void suspended_source_feeds_nothing ()
  {
    TestServer server {testPort (2)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    detector.setBlockSize (granted_frames_per_chunk);
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (2))};
    QSignalSpy written {&detector, &Detector::framesWritten};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);
    QVERIFY (source.isConnected ());
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    // start () leaves the source quiet; monitoring is what opens it. Injecting
    // now must reach the transport and stop there.
    for (int i = 0; i < 3; ++i)
      {
        server.inject (nativeChunk (i));
      }
    QTest::qWait (500);
    QVERIFY (written.isEmpty ());

    source.resume ();
    server.inject (nativeChunk (11));
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written),
                               qint64 {granted_frames_per_chunk}, 5000);

    source.suspend ();
    auto const after_suspend = written.count ();
    for (int i = 0; i < 3; ++i)
      {
        server.inject (nativeChunk (i));
      }
    QTest::qWait (500);
    QCOMPARE (written.count (), after_suspend);

    source.stop ();
  }

  Q_SLOT void rejects_a_format_the_decoders_cannot_use ()
  {
    // 12000 is not an integer divisor of 44100, so the server declines the
    // request and answers with its native format. The source must refuse that
    // rather than feed the decoders wrong-rate audio labelled as 12 kHz.
    TestServer server {testPort (3), 44100, 2};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (3))};
    QSignalSpy errors {&source, &AudioInputSource::error};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);

    QCOMPARE (errors.count (), 1);
    auto const message = errors.first ().first ().toString ();
    QVERIFY2 (message.contains ("44100"), qPrintable (message));
    QVERIFY2 (message.contains ("12000"), qPrintable (message));
    QVERIFY (!source.isConnected ());
    QVERIFY (!source.streamDescriptor ().isValid ());
  }

  Q_SLOT void a_left_channel_selection_reads_the_left_side ()
  {
    TestServer server {testPort (4)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    detector.setBlockSize (granted_frames_per_chunk);
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (4))};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy written {&detector, &Detector::framesWritten};

    // On a dual-receive rig the two channels are two VFOs, so Left is a real
    // choice. It is answered by AudioDevice::store () on a stereo stream, not
    // by the server.
    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Left);

    QVERIFY2 (errors.isEmpty (),
              qPrintable (errors.isEmpty () ? QString {}
                                            : errors.first ().first ().toString ()));
    QVERIFY (source.isConnected ());
    QCOMPARE (source.streamDescriptor ().channel_count, 2);

    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
    source.resume ();
    server.inject (nativeChunk (5));
    // One frame is one stereo pair, so a 20 ms chunk is still 240 detector
    // frames even though twice the bytes arrive.
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written),
                               qint64 {granted_frames_per_chunk}, 5000);
    source.stop ();
  }

  Q_SLOT void a_mono_server_is_usable ()
  {
    // Regression test. Asking for a channel layout as well as a rate is
    // declined IN FULL by a mono-native server -- layouts other than NATIVE
    // need a stereo native, and the grant is all-or-nothing -- so the rate
    // reduction was lost with it and this source refused to start. Serving
    // mono is the obvious way to halve bandwidth at the source, so this is a
    // configuration an operator will really choose.
    TestServer server {testPort (5), native_rate, 1};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    detector.setBlockSize (granted_frames_per_chunk);
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (5))};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy written {&detector, &Detector::framesWritten};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Left);

    QVERIFY2 (errors.isEmpty (),
              qPrintable (errors.isEmpty () ? QString {}
                                            : errors.first ().first ().toString ()));
    QVERIFY (source.isConnected ());
    QCOMPARE (source.streamDescriptor ().sample_rate_hz,
              NetworkAudioInput::detectorSampleRate);
    // One channel arrived, so the configured Left has nothing to select from
    // and the sink reads one sample per frame.
    QCOMPARE (source.streamDescriptor ().channel_count, 1);

    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
    source.resume ();
    server.inject (nativeChunk (7, 1));
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written),
                               qint64 {granted_frames_per_chunk}, 5000);
    source.stop ();
  }

  // A4. An outage is the case the period buffer cannot survive: libnaudio
  // reconnects on its own (1 s base delay, 10 attempts, both defaults), so
  // without discontinuity handling the audio from either side of the hole is
  // concatenated and every decode in that period carries a DT short by the
  // outage. The buffer must be reset instead.
  Q_SLOT void an_outage_resets_the_period_buffer ()
  {
    auto const port = testPort (7);
    auto server = std::make_unique<TestServer> (port);
    QVERIFY2 (server->started (), qPrintable (server->error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    detector.setBlockSize (granted_frames_per_chunk);
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (port)};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy written {&detector, &Detector::framesWritten};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);
    QVERIFY2 (errors.isEmpty (),
              qPrintable (errors.isEmpty () ? QString {}
                                            : errors.first ().first ().toString ()));
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
    source.resume ();

    constexpr int before {4};
    for (int i = 0; i < before; ++i)
      {
        server->inject (nativeChunk (i * 97));
      }
    qint64 const filled {qint64 {before} * granted_frames_per_chunk};
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written), filled, 5000);
    QCOMPARE (source.discontinuityCount (), qint64 {0});

    // The outage. Destroying the server drops the connection; libnaudio starts
    // reconnecting on its own.
    server.reset ();
    QTRY_VERIFY_WITH_TIMEOUT (!g_streaming.load (std::memory_order_acquire), 5000);
    // While libnaudio is reconnecting the link is down, and the source must
    // say so rather than report the last state it knew.
    QTRY_VERIFY_WITH_TIMEOUT (!source.isConnected (), 5000);

    // ... and the stream comes back on the same endpoint.
    server = std::make_unique<TestServer> (port);
    QVERIFY2 (server->started (), qPrintable (server->error ()));
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 20000);

    constexpr int after {3};
    for (int i = 0; i < after; ++i)
      {
        server->inject (nativeChunk (5000 + i * 97));
      }

    // The reset happens on the first chunk to arrive after the hole, so the
    // frame count covers only post-outage audio. Without the reset it would be
    // filled + this, with the two halves of the outage spliced together.
    qint64 const expected {qint64 {after} * granted_frames_per_chunk};
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written), expected, 10000);
    QVERIFY2 (source.discontinuityCount () > 0,
              "the outage should have raised at least one discontinuity");

    source.stop ();
  }

  // The counter-driven half must not fire on a healthy stream. sequence_gaps
  // counts control and parity slots as well as audio, so a naive read of it
  // would reset the period buffer on a stream that lost nothing -- costing a
  // period of decodes to fix a problem that did not exist. On the transport
  // this source uses (TCP) libnaudio measures neither counter, the first poll
  // finds both at -1 and retires the timer, and detection is by link events
  // alone; what this test can establish on TCP is therefore that neither the
  // poll nor the events raise a discontinuity the stream did not have. The
  // counter arithmetic itself is exercised only where a UDP profile measures
  // it, which this source does not yet select.
  Q_SLOT void a_clean_stream_raises_no_discontinuity ()
  {
    TestServer server {testPort (8)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    detector.setBlockSize (granted_frames_per_chunk);
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (8))};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy written {&detector, &Detector::framesWritten};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);
    QVERIFY (errors.isEmpty ());
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
    source.resume ();

    // Long enough to cross at least two stats polls (1 s each): the poll is
    // where a spurious reset would come from, so a test that never polls would
    // not test anything.
    QElapsedTimer clock;
    clock.start ();
    int chunks {0};
    while (clock.elapsed () < 2500)
      {
        server.inject (nativeChunk (chunks * 31));
        ++chunks;
        QTest::qWait (20);
      }

    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written),
                               qint64 {chunks} * granted_frames_per_chunk, 5000);
    QCOMPARE (source.discontinuityCount (), qint64 {0});
    QVERIFY (errors.isEmpty ());

    source.stop ();
    // The count survives stop () so a caller can ask what the session saw.
    QCOMPARE (source.discontinuityCount (), qint64 {0});
  }

  // The link that cannot come back is an error, not silence. libnaudio fires
  // on_disconnected only when it has given up (it is the END of an outage, not
  // the start), and the handle is spent from then on; the source must report
  // that through error () and stop claiming a stream it does not have. A short
  // reconnect policy keeps the test short.
  Q_SLOT void an_unrecoverable_outage_is_reported_as_an_error ()
  {
    auto const port = testPort (10);
    auto server = std::make_unique<TestServer> (port);
    QVERIFY2 (server->started (), qPrintable (server->error ()));

    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (port)};
    source.setReconnectPolicy (100, 100, 2);
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy statuses {&source, &AudioInputSource::status};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);
    QVERIFY (errors.isEmpty ());
    QVERIFY (source.isConnected ());
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);

    server.reset ();

    QTRY_COMPARE_WITH_TIMEOUT (errors.count (), 1, 10000);
    auto const message = errors.first ().first ().toString ();
    QVERIFY2 (message.contains ("could not be re-established"), qPrintable (message));
    QVERIFY (!source.isConnected ());
    QVERIFY (!source.streamDescriptor ().isValid ());
    // The outage was reported while it was still an outage, before it became
    // an error.
    bool dropped {false};
    for (auto const& s : statuses)
      {
        if (s.first ().toString ().contains ("reconnecting")) dropped = true;
      }
    QVERIFY (dropped);
  }

  // D3 of the remote-operation plan. The transmit path will open the TX lane
  // with NA_DUPLEX_TALK on the connection this source shares. TALK mutes the
  // client's local PLAYBACK, which this program never uses -- RX audio reaches
  // this source through the receive callback, not through playback -- so RX
  // is expected to keep flowing. Expected is not proven; this proves it, on
  // one connection that both paths will use, opened with the transmit lane.
  Q_SLOT void rx_keeps_flowing_while_the_duplex_mode_is_talk ()
  {
    TestServer server {testPort (11)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));

    // A transmitting connection subscribes at the server's native rate (the
    // transmit path has no other way to learn it), so this source runs the
    // application's 4:1 decimation: one native 960-frame chunk fills the
    // detector's 4 x 240 buffer and advances kin by the same 240 frames.
    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 4};
    detector.setBlockSize (granted_frames_per_chunk);
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (11))};
    // Decided at connect: the lane exists only if this was set before open ().
    connection.setTransmitEnabled (true);
    NetworkAudioInput source {&connection};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy written {&detector, &Detector::framesWritten};

    source.start (QAudioDeviceInfo {}, 0, &detector, 4, AudioDevice::Mono);
    QCOMPARE (connection.audioFormat ().sample_rate_hz, native_rate);
    QVERIFY2 (errors.isEmpty (),
              qPrintable (errors.isEmpty () ? QString {}
                                            : errors.first ().first ().toString ()));
    QVERIFY (connection.isOpen ());
    QCOMPARE (source.connection (), &connection);
    QTRY_VERIFY_WITH_TIMEOUT (g_streaming.load (std::memory_order_acquire), 5000);
    source.resume ();

    constexpr int listening {3};
    for (int i = 0; i < listening; ++i)
      {
        server.inject (nativeChunk (i * 97));
      }
    qint64 const before {qint64 {listening} * granted_frames_per_chunk};
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written), before, 5000);

    QVERIFY (connection.setDuplex (NetworkAudioConnection::Duplex::Talk));
    constexpr int talking {3};
    for (int i = 0; i < talking; ++i)
      {
        server.inject (nativeChunk (1000 + i * 97));
      }
    qint64 const during {before + qint64 {talking} * granted_frames_per_chunk};
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written), during, 5000);

    QVERIFY (connection.setDuplex (NetworkAudioConnection::Duplex::Listen));
    server.inject (nativeChunk (2000));
    QTRY_COMPARE_WITH_TIMEOUT (lastFramesWritten (written),
                               during + granted_frames_per_chunk, 5000);
    QVERIFY (errors.isEmpty ());

    // Stopping a source that BORROWS its connection leaves the connection
    // open: the transmit path may be using it. Closing is the owner's call.
    source.stop ();
    QVERIFY (!source.isConnected ());
    QVERIFY (connection.isOpen ());
    QVERIFY (connection.isConnected ());
    connection.close ();
    QVERIFY (!connection.isOpen ());
  }

  Q_SLOT void reports_an_unreachable_server ()
  {
    Detector detector {RX_SAMPLE_RATE, no_wrap_period, 1};
    detector.setBlockSize (granted_frames_per_chunk);
    // Nothing is listening here.
    NetworkAudioInput source {"127.0.0.1", static_cast<quint16> (testPort (9))};
    QSignalSpy errors {&source, &AudioInputSource::error};
    QSignalSpy written {&detector, &Detector::framesWritten};

    source.start (QAudioDeviceInfo {}, 0, &detector, 1, AudioDevice::Mono);

    QCOMPARE (errors.count (), 1);
    QVERIFY (!source.isConnected ());
    QVERIFY (written.isEmpty ());
  }
};

QTEST_MAIN (TestNetworkAudioInput)

#include "test_network_audio_input.moc"
