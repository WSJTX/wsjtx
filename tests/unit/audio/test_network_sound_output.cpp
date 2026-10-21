#include <QtTest>

#include <QBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QVector>

#include <atomic>
#include <memory>

#include "naudio.h"

#include "Audio/AudioDevice.hpp"
#include "Audio/NetworkAudioConnection.hpp"
#include "Audio/NetworkSoundOutput.hpp"
#include "Audio/TxPlaybackEvidence.hpp"

namespace
{
  constexpr int native_rate {48000};
  constexpr int native_channels {2};

  // A modulator stand-in: an AudioDevice whose frames count up from one, so
  // that nothing it produces is silence and every frame is identifiable.
  class RampSource final
    : public AudioDevice
  {
  public:
    explicit RampSource (qint64 frames, AudioDevice::Channel channel = AudioDevice::Mono)
      : m_frames {frames}
    {
      initialize (QIODevice::ReadOnly, channel);
    }
    qint64 frames () const {return m_frames;}
    qint64 served () const {return m_served;}
    static qint16 value (qint64 frame) {return static_cast<qint16> (1 + frame % 4000);}

  protected:
    qint64 readData (char * data, qint64 maxSize) override
    {
      auto const frames = qMin (maxSize / qint64 (bytesPerFrame ()), m_frames - m_served);
      auto * samples = reinterpret_cast<qint16 *> (data);
      for (qint64 i = 0; i < frames; ++i)
        {
          samples = load (value (m_served + i), samples);
        }
      m_served += frames;
      return frames * bytesPerFrame ();
    }
    qint64 writeData (char const *, qint64) override {return -1;}

  private:
    qint64 m_frames;
    qint64 m_served {0};
  };

  // Everything the NULL backend's mixer hands out, in arrival order. Silence
  // frames arrive continuously whether or not anyone transmits.
  struct Captured
  {
    QMutex mutex;
    QByteArray pcm;
    long long callbacks {0};
  };

  void on_tx_audio (unsigned char const * pcm, size_t n_bytes, void * user)
  {
    auto * captured = static_cast<Captured *> (user);
    QMutexLocker lock {&captured->mutex};
    captured->pcm.append (reinterpret_cast<char const *> (pcm), static_cast<int> (n_bytes));
    ++captured->callbacks;
  }

  // An in-process, hardware-free naudio server that collects its mixed TX
  // audio: the desk stand-in for the station host's playback device.
  class TestServer
  {
  public:
    explicit TestServer (int port, int rate = native_rate, int channels = native_channels)
      : channels_ {channels}
    {
      server_ = na_server_create (NA_SERVER_BACKEND_NULL, port);
      if (!server_) return;
      na_server_set_audio_format (server_, rate, 16, channels);
      na_server_set_tx_audio_cb (server_, &on_tx_audio, &captured_);
      char err[256] {};
      started_ = NA_OK == na_server_start (server_, err, sizeof err);
      error_ = QString::fromUtf8 (err);
    }
    ~TestServer () {if (server_) na_server_destroy (server_);}
    TestServer (TestServer const&) = delete;
    TestServer& operator= (TestServer const&) = delete;

    bool started () const {return started_;}
    QString const& error () const {return error_;}
    int channels () const {return channels_;}
    QString owner () const
    {
      char buf[128] {};
      auto const n = na_server_tx_owner (server_, buf, sizeof buf);
      return n > 0 ? QString::fromUtf8 (buf, qMin<int> (n, sizeof buf - 1)) : QString {};
    }
    QByteArray received () const
    {
      QMutexLocker lock {&captured_.mutex};
      return captured_.pcm;
    }
    long long callbacks () const
    {
      QMutexLocker lock {&captured_.mutex};
      return captured_.callbacks;
    }

  private:
    na_audio_server * server_ {nullptr};
    mutable Captured captured_;
    int channels_;
    bool started_ {false};
    QString error_;
  };

  QVector<qint16> channel (QByteArray const& pcm, int channels, int which)
  {
    auto const * samples = reinterpret_cast<qint16 const *> (pcm.constData ());
    auto const frames = pcm.size () / (2 * channels);
    QVector<qint16> out;
    out.reserve (frames);
    for (int f = 0; f < frames; ++f) out.append (samples[f * channels + which]);
    return out;
  }

  // The ramp as it should appear on one channel: every non-zero sample in
  // order, and the longest run of zero frames between the first and last of
  // them (a gap the pacing let through).
  struct RampReading
  {
    qint64 samples {0};
    qint64 first_mismatch {-1};
    qint16 expected {0};
    qint16 found {0};
    qint64 longest_gap {0};
    int first {-1};
    int last {-1};
  };

  RampReading readRamp (QVector<qint16> const& ch)
  {
    RampReading r;
    qint64 run {0};
    for (int i = 0; i < ch.size (); ++i)
      {
        if (!ch[i])
          {
            if (r.first >= 0) ++run;
            continue;
          }
        if (r.first < 0) r.first = i;
        r.last = i;
        r.longest_gap = qMax (r.longest_gap, run);
        run = 0;
        auto const expected = RampSource::value (r.samples);
        if (ch[i] != expected && r.first_mismatch < 0)
          {
            r.first_mismatch = r.samples;
            r.expected = expected;
            r.found = ch[i];
          }
        ++r.samples;
      }
    return r;
  }

  // A source whose every read fails.
  class BrokenSource final
    : public QIODevice
  {
  public:
    BrokenSource () {open (QIODevice::ReadOnly);}

  protected:
    qint64 readData (char *, qint64) override {return -1;}
    qint64 writeData (char const *, qint64) override {return -1;}
  };

  int testPort (int offset) {return 45600 + offset;}

  TxEvidence::TxOutputError fault (QSignalSpy const& spy, int index = 0)
  {
    return qvariant_cast<TxEvidence::TxOutputError> (spy.at (index).first ());
  }

  QString firstError (QSignalSpy const& spy)
  {
    return spy.isEmpty () ? QString {} : fault (spy).message;
  }

  QVector<TxEvidence::TxRawPlayoutSnapshot> snapshotsOf (QSignalSpy const& spy)
  {
    QVector<TxEvidence::TxRawPlayoutSnapshot> out;
    for (auto const& args : spy)
      {
        out.append (qvariant_cast<TxEvidence::TxRawPlayoutSnapshot> (args.first ()));
      }
    return out;
  }

  TxEvidence::TxPlaybackContext const context_a {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
  TxEvidence::TxPlaybackContext const context_b {TxEvidence::TxSessionId {102}, TxEvidence::TxGeneration {202}};
}

class TestNetworkSoundOutput
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

  Q_SLOT void refuses_a_connection_opened_without_the_transmit_lane ()
  {
    TestServer server {testPort (0)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (0))};
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));

    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    RampSource ramp {native_rate};
    sink.restart (&ramp, context_a);

    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("transmit lane"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (fault (errors).context, context_a);
    QVERIFY (!sink.isActive ());
    QCOMPARE (sink.framesSent (), qint64 {0});
    QCOMPARE (ramp.served (), qint64 {0});
    QTest::qWait (300);
    QVERIFY (server.owner ().isEmpty ());
    connection.close ();
  }

  // D4: the modulator is fixed at 48 kHz and net-audio does not resample
  // injected audio, so a server provisioned at another rate is refused by
  // name rather than fed audio at the wrong pitch.
  Q_SLOT void refuses_a_server_that_is_not_at_the_modulators_rate ()
  {
    TestServer server {testPort (1), 24000};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (1))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (24000), qPrintable (connection.lastError ()));
    QCOMPARE (connection.audioFormat ().sample_rate_hz, 24000);

    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    RampSource ramp {native_rate};
    sink.restart (&ramp, context_a);

    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("24000"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (fault (errors).context, context_a);
    QVERIFY (!sink.isActive ());
    QCOMPARE (ramp.served (), qint64 {0});
    connection.close ();
  }

  // Dragon 1, the sink is the clock: a full FT8 transmission's worth of
  // audio, paced into the server's mixer, arrives complete and in order with
  // no gap longer than one block -- and the server's TX ownership names this
  // client while it runs and releases after it stops.
  Q_SLOT void paces_a_full_transmission_into_the_server ()
  {
    TestServer server {testPort (2)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (2))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));

    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    QSignalSpy idle {&sink, &AudioOutputSink::audioOutputIdle};
    QSignalSpy owners {&sink, &NetworkSoundOutput::transmitOwnerChanged};

    constexpr qint64 seconds {13};
    RampSource ramp {seconds * native_rate};
    sink.restart (&ramp, context_a);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QVERIFY (sink.isActive ());

    QTRY_VERIFY_WITH_TIMEOUT (!server.owner ().isEmpty (), 5000);
    auto const owner_while_sending = server.owner ();
    QTRY_COMPARE_WITH_TIMEOUT (sink.transmitOwner (), owner_while_sending, 5000);

    QTRY_VERIFY_WITH_TIMEOUT (sink.isExhausted (), (seconds + 5) * 1000);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QCOMPARE (idle.count (), 1);
    QCOMPARE (sink.framesSent (), ramp.frames ());
    QCOMPARE (sink.shortInjects (), qint64 {0});
    QVERIFY (sink.isActive ());   // the lane stays open until stop ()

    QTest::qWait (500);           // let the mixer play out what it holds
    sink.stop ();
    QVERIFY (!sink.isActive ());
    QCOMPARE (idle.count (), 2);
    QTRY_VERIFY_WITH_TIMEOUT (server.owner ().isEmpty (), 5000);
    QVERIFY (owners.count () >= 1);

    auto const pcm = server.received ();
    auto const left = channel (pcm, native_channels, 0);
    auto const right = channel (pcm, native_channels, 1);
    auto const reading = readRamp (left);
    qInfo () << "received" << left.size () << "frames in" << server.callbacks () << "callbacks;"
             << "ramp samples" << reading.samples << "of" << ramp.frames ()
             << "longest internal gap" << reading.longest_gap << "frames";
    QVERIFY2 (reading.first_mismatch < 0,
              qPrintable (QString {"ramp broke at sample %1: expected %2, found %3"}
                          .arg (reading.first_mismatch).arg (reading.expected).arg (reading.found)));
    QCOMPARE (reading.samples, ramp.frames ());
    QVERIFY2 (reading.longest_gap <= NetworkSoundOutput::blockFrames,
              qPrintable (QString {"a gap of %1 frames"}.arg (reading.longest_gap)));
    // Mono source, stereo server: the sample goes to both sides.
    QCOMPARE (right.mid (reading.first, reading.last - reading.first + 1),
              left.mid (reading.first, reading.last - reading.first + 1));
    connection.close ();
  }

  // D5: the modulator expands its mono signal to the configured channel;
  // the sink puts it on the wire in the server's channel count.
  Q_SLOT void maps_the_source_channel_onto_the_servers_channels ()
  {
    {
      TestServer server {testPort (3)};
      QVERIFY2 (server.started (), qPrintable (server.error ()));
      NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (3))};
      connection.setTransmitEnabled (true);
      QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
      NetworkSoundOutput sink;
      sink.setConnection (&connection);
      QSignalSpy errors {&sink, &AudioOutputSink::error};

      RampSource left_only {native_rate / 2, AudioDevice::Left};
      sink.restart (&left_only, context_a);
      QTRY_VERIFY_WITH_TIMEOUT (sink.isExhausted (), 5000);
      QTest::qWait (300);
      sink.stop ();
      QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
      auto const pcm = server.received ();
      auto const left = readRamp (channel (pcm, 2, 0));
      auto const right = readRamp (channel (pcm, 2, 1));
      QCOMPARE (left.samples, left_only.frames ());
      QVERIFY (left.first_mismatch < 0);
      QCOMPARE (right.samples, qint64 {0});
      connection.close ();
    }
    {
      // One channel on the wire: the side the source was told to use.
      TestServer server {testPort (4), native_rate, 1};
      QVERIFY2 (server.started (), qPrintable (server.error ()));
      NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (4))};
      connection.setTransmitEnabled (true);
      QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
      QCOMPARE (connection.audioFormat ().channel_count, 1);
      NetworkSoundOutput sink;
      sink.setConnection (&connection);
      QSignalSpy errors {&sink, &AudioOutputSink::error};

      RampSource right_only {native_rate / 2, AudioDevice::Right};
      sink.restart (&right_only, context_a);
      QTRY_VERIFY_WITH_TIMEOUT (sink.isExhausted (), 5000);
      QTest::qWait (300);
      sink.stop ();
      QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
      auto const mono = readRamp (channel (server.received (), 1, 0));
      QCOMPARE (mono.samples, right_only.frames ());
      QVERIFY (mono.first_mismatch < 0);
      connection.close ();
    }
  }

  // D3: stop () returns the lane to Listen and the server releases TX
  // ownership; the next transmission reopens it on the same connection.
  Q_SLOT void stop_returns_the_lane_to_listen_and_the_next_restart_reopens_it ()
  {
    TestServer server {testPort (5)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (5))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};

    RampSource first {native_rate / 2};
    sink.restart (&first, context_a);
    QTRY_VERIFY_WITH_TIMEOUT (!server.owner ().isEmpty (), 5000);
    QTRY_VERIFY_WITH_TIMEOUT (sink.isExhausted (), 5000);
    sink.stop ();
    QTRY_VERIFY_WITH_TIMEOUT (server.owner ().isEmpty (), 5000);
    QVERIFY (connection.isOpen () && connection.isConnected ());

    auto const before = server.received ().size ();
    RampSource second {native_rate / 2};
    sink.restart (&second, context_b);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QTRY_VERIFY_WITH_TIMEOUT (!server.owner ().isEmpty (), 5000);
    QTRY_VERIFY_WITH_TIMEOUT (sink.isExhausted (), 5000);
    QTest::qWait (300);
    sink.stop ();
    auto const after = readRamp (channel (server.received ().mid (before), 2, 0));
    QCOMPARE (after.samples, second.frames ());
    QVERIFY (after.first_mismatch < 0);
    connection.close ();
  }

  // Dragon 6: a lost link fails the transmission with one named error rather
  // than leaving a sink pacing into nothing.
  Q_SLOT void a_lost_link_fails_the_transmission ()
  {
    auto server = std::make_unique<TestServer> (testPort (6));
    QVERIFY2 (server->started (), qPrintable (server->error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (6))};
    connection.setTransmitEnabled (true);
    connection.setReconnectPolicy (100, 100, 2);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    QSignalSpy idle {&sink, &AudioOutputSink::audioOutputIdle};
    QSignalSpy snapshots {&sink, &AudioOutputSink::rawTxPlayoutSnapshot};

    RampSource ramp {10 * native_rate};
    sink.restart (&ramp, context_a);
    QTRY_VERIFY_WITH_TIMEOUT (!server->owner ().isEmpty (), 5000);
    server.reset ();

    QTRY_VERIFY_WITH_TIMEOUT (!errors.isEmpty (), 5000);
    QVERIFY2 (firstError (errors).contains ("lost") || firstError (errors).contains ("not accepting"),
              qPrintable (firstError (errors)));
    // Io stops the transmission: no transmitter left keyed on a broken path.
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors).context, context_a);
    QVERIFY (!sink.isActive ());
    QCOMPARE (idle.count (), 1);
    QVERIFY (sink.framesSent () < ramp.frames ());
    // The transmission's last snapshot is the fault, with what was sent.
    auto const last = snapshotsOf (snapshots).back ();
    QCOMPARE (last.context, context_a);
    QCOMPARE (last.backend_start_sequence, qint64 {1});
    QVERIFY (!last.start_event);
    QCOMPARE (last.tier, TxEvidence::TxPlayoutTier::DeadReckoning);
    QCOMPARE (last.state, TxEvidence::TxBackendState::Stopped);
    QCOMPARE (last.error, TxEvidence::TxBackendError::Io);
    QCOMPARE (last.successfully_written_frames, sink.framesAccepted ());
    // The stop MainWindow sends after the fault keeps the fault on record.
    sink.stop ();
    auto const after_stop = snapshotsOf (snapshots).back ();
    QCOMPARE (after_stop.state, TxEvidence::TxBackendState::Stopped);
    QCOMPARE (after_stop.backend_start_sequence, qint64 {1});
    QCOMPARE (after_stop.error, TxEvidence::TxBackendError::Io);
    QCOMPARE (after_stop.diagnostic, firstError (errors));
    connection.close ();
  }

  // Every snapshot and every fault carries the context of the restart () it
  // belongs to and that restart's sequence. A restart that opens nothing
  // publishes an Unavailable start snapshot ahead of its fault, as the sound
  // card's sink does.
  Q_SLOT void stamps_snapshots_and_faults_with_their_restarts_context ()
  {
    TestServer server {testPort (7)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (7))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    QSignalSpy snapshots {&sink, &AudioOutputSink::rawTxPlayoutSnapshot};

    RampSource first {native_rate / 2};
    sink.restart (&first, context_a);
    QTRY_VERIFY_WITH_TIMEOUT (sink.isExhausted (), 5000);
    sink.stop ();
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));

    auto const played = snapshotsOf (snapshots);
    QVERIFY (played.size () >= 3);
    QVERIFY (played.front ().start_event);
    QCOMPARE (played.front ().state, TxEvidence::TxBackendState::Active);
    bool idle_seen {false};
    for (int i = 0; i < played.size (); ++i)
      {
        auto const& snapshot = played.at (i);
        QCOMPARE (snapshot.context, context_a);
        QCOMPARE (snapshot.backend_start_sequence, qint64 {1});
        QCOMPARE (snapshot.start_event, 0 == i);
        QCOMPARE (snapshot.tier, TxEvidence::TxPlayoutTier::DeadReckoning);
        QVERIFY (snapshot.available);
        QCOMPARE (snapshot.error, TxEvidence::TxBackendError::None);
        idle_seen = idle_seen || TxEvidence::TxBackendState::Idle == snapshot.state;
      }
    QVERIFY (idle_seen);
    QCOMPARE (played.back ().state, TxEvidence::TxBackendState::Stopped);
    QCOMPARE (played.back ().successfully_written_frames, first.frames ());

    snapshots.clear ();
    QStringList order;
    connect (&sink, &AudioOutputSink::rawTxPlayoutSnapshot, this, [&order] {order << "snapshot";});
    connect (&sink, &AudioOutputSink::error, this, [&order] {order << "error";});
    sink.restart (nullptr, context_b);
    QCOMPARE (errors.count (), 1);
    QCOMPARE (fault (errors).context, context_b);
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (order, (QStringList {"snapshot", "error"}));
    auto const refused = snapshotsOf (snapshots);
    QCOMPARE (refused.size (), 1);
    QVERIFY (refused.front ().start_event);
    QVERIFY (!refused.front ().available);
    QCOMPARE (refused.front ().state, TxEvidence::TxBackendState::Unavailable);
    QCOMPARE (refused.front ().error, TxEvidence::TxBackendError::Open);
    QCOMPARE (refused.front ().context, context_b);
    QCOMPARE (refused.front ().backend_start_sequence, qint64 {2});

    // Nothing open to stop: an Unavailable snapshot under the same restart.
    sink.stop ();
    auto const stopped = snapshotsOf (snapshots);
    QCOMPARE (stopped.size (), 2);
    QVERIFY (!stopped.back ().start_event);
    QCOMPARE (stopped.back ().state, TxEvidence::TxBackendState::Unavailable);
    QCOMPARE (stopped.back ().context, context_b);
    QCOMPARE (stopped.back ().backend_start_sequence, qint64 {2});
    connection.close ();
  }

  // A restart () that cannot open the lane is an Open fault, on which the
  // transmission stops instead of keying into nothing.
  Q_SLOT void refusals_at_restart_are_open_faults ()
  {
    NetworkSoundOutput sink;
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    RampSource ramp {native_rate};

    sink.restart (&ramp, context_a);
    QCOMPARE (errors.count (), 1);
    QVERIFY2 (fault (errors, 0).message.contains ("no network audio connection"),
              qPrintable (fault (errors, 0).message));
    QCOMPARE (fault (errors, 0).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (fault (errors, 0).context, context_a);

    NetworkAudioConnection closed {"127.0.0.1", static_cast<quint16> (testPort (8))};
    closed.setTransmitEnabled (true);
    sink.setConnection (&closed);
    sink.restart (&ramp, context_b);
    QCOMPARE (errors.count (), 2);
    QVERIFY2 (fault (errors, 1).message.contains ("to be open"), qPrintable (fault (errors, 1).message));
    QCOMPARE (fault (errors, 1).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (fault (errors, 1).context, context_b);

    sink.restart (nullptr, context_a);
    QCOMPARE (errors.count (), 3);
    QVERIFY2 (fault (errors, 2).message.contains ("readable"), qPrintable (fault (errors, 2).message));
    QCOMPARE (fault (errors, 2).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (fault (errors, 2).context, context_a);

    QVERIFY (!sink.isActive ());
    QCOMPARE (ramp.served (), qint64 {0});
  }

  // Whatever breaks a running transmission -- a changed or destroyed
  // connection, a source that fails a read or hands over part of a frame --
  // is an Io fault under the context the transmission started with.
  Q_SLOT void faults_during_a_transmission_are_io_faults ()
  {
    TestServer server {testPort (9)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    auto connection = std::make_unique<NetworkAudioConnection> (
      "127.0.0.1", static_cast<quint16> (testPort (9)));
    connection->setTransmitEnabled (true);
    QVERIFY2 (connection->open (native_rate), qPrintable (connection->lastError ()));
    NetworkAudioConnection other {"127.0.0.1", static_cast<quint16> (testPort (9))};
    NetworkSoundOutput sink;
    QSignalSpy errors {&sink, &AudioOutputSink::error};

    sink.setConnection (connection.get ());
    RampSource first {10 * native_rate};
    sink.restart (&first, context_a);
    QVERIFY (sink.isActive ());
    sink.setConnection (&other);
    QCOMPARE (errors.count (), 1);
    QVERIFY2 (fault (errors, 0).message.contains ("changed"), qPrintable (fault (errors, 0).message));
    QCOMPARE (fault (errors, 0).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors, 0).context, context_a);
    QVERIFY (!sink.isActive ());

    sink.setConnection (connection.get ());
    BrokenSource broken;
    sink.restart (&broken, context_b);
    QCOMPARE (errors.count (), 2);
    QVERIFY2 (fault (errors, 1).message.contains ("could not read"), qPrintable (fault (errors, 1).message));
    QCOMPARE (fault (errors, 1).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors, 1).context, context_b);

    QBuffer odd;
    odd.setData (QByteArray (3, '\x01'));
    QVERIFY (odd.open (QIODevice::ReadOnly));
    sink.restart (&odd, context_a);
    QCOMPARE (errors.count (), 3);
    QVERIFY2 (fault (errors, 2).message.contains ("partial frame"), qPrintable (fault (errors, 2).message));
    QCOMPARE (fault (errors, 2).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors, 2).context, context_a);

    // The lane closed under the transmission: the library accepts nothing.
    RampSource second {10 * native_rate};
    sink.restart (&second, context_b);
    QVERIFY (sink.isActive ());
    QVERIFY (connection->setDuplex (NetworkAudioConnection::Duplex::Listen));
    QTRY_COMPARE_WITH_TIMEOUT (errors.count (), 4, 3000);
    QVERIFY2 (fault (errors, 3).message.contains ("not accepting"), qPrintable (fault (errors, 3).message));
    QCOMPARE (fault (errors, 3).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors, 3).context, context_b);
    QVERIFY (!sink.isActive ());

    // The connection destroyed under a suspended transmission. Destroying it
    // disconnects it, and its queued linkLost would otherwise race the pump
    // that resume () runs at once.
    RampSource third {10 * native_rate};
    sink.restart (&third, context_a);
    QVERIFY (sink.isActive ());
    sink.suspend ();
    connection.reset ();
    sink.resume ();
    QCOMPARE (errors.count (), 5);
    QVERIFY2 (fault (errors, 4).message.contains ("disappeared"), qPrintable (fault (errors, 4).message));
    QCOMPARE (fault (errors, 4).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors, 4).context, context_a);
    QVERIFY (!sink.isActive ());
    QTest::qWait (100);
    QCOMPARE (errors.count (), 5);
  }

  // A connection libnaudio has given up on stays open (linkLost, reconnects
  // exhausted) but is not connected; a transmission on it is refused at once
  // rather than keying the radio into a dead handle until the zero-accept run.
  Q_SLOT void refuses_a_link_that_is_down ()
  {
    auto server = std::make_unique<TestServer> (testPort (10));
    QVERIFY2 (server->started (), qPrintable (server->error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (10))};
    connection.setTransmitEnabled (true);
    connection.setReconnectPolicy (100, 100, 2);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    QSignalSpy lost {&connection, &NetworkAudioConnection::linkLost};
    server.reset ();
    QTRY_VERIFY_WITH_TIMEOUT (!lost.isEmpty (), 10000);
    QVERIFY (connection.isOpen ());
    QVERIFY (!connection.isConnected ());

    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    RampSource ramp {native_rate};
    sink.restart (&ramp, context_a);
    QCOMPARE (errors.count (), 1);
    QVERIFY2 (firstError (errors).contains ("down"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Open);
    QCOMPARE (fault (errors).context, context_a);
    QVERIFY (!sink.isActive ());
    QCOMPARE (ramp.served (), qint64 {0});
    connection.close ();
  }

  // The snapshots report the frames the library accepted. Blocks it refused
  // while the lane was closed under the transmission are paced but not
  // counted, so the evidence matches what reached the server's mixer, less at
  // most the lead the ring held when the lane closed.
  Q_SLOT void snapshots_count_only_the_frames_the_library_accepted ()
  {
    TestServer server {testPort (11)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (11))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    QSignalSpy snapshots {&sink, &AudioOutputSink::rawTxPlayoutSnapshot};

    RampSource ramp {30 * native_rate};
    sink.restart (&ramp, context_a);
    QTest::qWait (500);
    QVERIFY (connection.setDuplex (NetworkAudioConnection::Duplex::Listen));
    QTRY_VERIFY_WITH_TIMEOUT (!errors.isEmpty (), 5000);
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Io);
    QTest::qWait (800);           // let the mixer play out what it holds

    auto const written = snapshotsOf (snapshots).back ().successfully_written_frames;
    auto const mixed = readRamp (channel (server.received (), native_channels, 0)).samples;
    qInfo () << "frames paced" << sink.framesSent () << "accepted" << sink.framesAccepted ()
             << "reported written" << written << "mixed by the server" << mixed;
    QCOMPARE (written, sink.framesAccepted ());
    QVERIFY (sink.framesSent () > sink.framesAccepted ());
    QVERIFY2 (written >= mixed && written - mixed <= qint64 {NetworkSoundOutput::leadBlocks} * NetworkSoundOutput::blockFrames,
              qPrintable (QString {"reported %1 frames written, the server mixed %2"}.arg (written).arg (mixed)));
    connection.close ();
  }

  // A fault during a transmission returns the lane to Listen, so nothing more
  // this client injects reaches the server.
  Q_SLOT void a_fault_returns_the_lane_to_listen ()
  {
    TestServer server {testPort (12)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (12))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    QByteArray const block (NetworkSoundOutput::blockFrames * 2 * native_channels, '\x01');
    // The observable: Talk accepts injected audio and Listen accepts none.
    QVERIFY (connection.setDuplex (NetworkAudioConnection::Duplex::Talk));
    QVERIFY (connection.injectTransmitAudio (block.constData (), block.size ()) > 0);
    QVERIFY (connection.setDuplex (NetworkAudioConnection::Duplex::Listen));
    QCOMPARE (connection.injectTransmitAudio (block.constData (), block.size ()), 0);

    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    BrokenSource broken;
    sink.restart (&broken, context_a);   // opens the lane, then fails its first read
    QCOMPARE (errors.count (), 1);
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (connection.injectTransmitAudio (block.constData (), block.size ()), 0);
    connection.close ();
  }

  // A linkLost from an earlier generation of the connection -- queued before a
  // close () and delivered after the next open () -- must not end the current
  // transmission; one from the current generation does.
  Q_SLOT void only_the_current_generations_link_loss_ends_a_transmission ()
  {
    TestServer server {testPort (13)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (13))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};

    RampSource ramp {10 * native_rate};
    sink.restart (&ramp, context_a);
    QVERIFY (sink.isActive ());
    QVERIFY (connection.generation () > 0);
    Q_EMIT connection.linkLost (QStringLiteral ("stale"), connection.generation () - 1);
    QTest::qWait (200);
    QVERIFY2 (errors.isEmpty (), qPrintable (firstError (errors)));
    QVERIFY (sink.isActive ());

    Q_EMIT connection.linkLost (QStringLiteral ("current"), connection.generation ());
    QTRY_COMPARE_WITH_TIMEOUT (errors.count (), 1, 2000);
    QVERIFY2 (firstError (errors).contains ("current"), qPrintable (firstError (errors)));
    QCOMPARE (fault (errors).error, TxEvidence::TxBackendError::Io);
    QCOMPARE (fault (errors).context, context_a);
    connection.close ();
  }

  // A restart after a faulted transmission starts its evidence afresh: its
  // snapshots carry no fault and count only its own frames.
  Q_SLOT void a_restart_after_a_fault_reports_only_its_own_transmission ()
  {
    TestServer server {testPort (14)};
    QVERIFY2 (server.started (), qPrintable (server.error ()));
    NetworkAudioConnection connection {"127.0.0.1", static_cast<quint16> (testPort (14))};
    connection.setTransmitEnabled (true);
    QVERIFY2 (connection.open (native_rate), qPrintable (connection.lastError ()));
    NetworkSoundOutput sink;
    sink.setConnection (&connection);
    QSignalSpy errors {&sink, &AudioOutputSink::error};
    QSignalSpy snapshots {&sink, &AudioOutputSink::rawTxPlayoutSnapshot};

    RampSource first {10 * native_rate};
    sink.restart (&first, context_a);
    QTRY_VERIFY_WITH_TIMEOUT (sink.framesAccepted () > 0, 5000);
    // A link loss with no reason from the library ends the sentence there.
    Q_EMIT connection.linkLost (QString {}, connection.generation ());
    QTRY_COMPARE_WITH_TIMEOUT (errors.count (), 1, 2000);
    QCOMPARE (firstError (errors), QStringLiteral ("The network audio link was lost during a transmission."));
    sink.stop ();
    QCOMPARE (snapshotsOf (snapshots).back ().error, TxEvidence::TxBackendError::Io);

    snapshots.clear ();
    RampSource second {native_rate / 2};
    sink.restart (&second, context_b);
    QTRY_VERIFY_WITH_TIMEOUT (sink.isExhausted (), 5000);
    sink.stop ();
    QCOMPARE (errors.count (), 1);
    auto const fresh = snapshotsOf (snapshots);
    QVERIFY (fresh.size () >= 3);
    for (auto const& snapshot : fresh)
      {
        QCOMPARE (snapshot.context, context_b);
        QCOMPARE (snapshot.error, TxEvidence::TxBackendError::None);
      }
    QCOMPARE (fresh.back ().successfully_written_frames, second.frames ());
    connection.close ();
  }
};

QTEST_MAIN (TestNetworkSoundOutput)

#include "test_network_sound_output.moc"
