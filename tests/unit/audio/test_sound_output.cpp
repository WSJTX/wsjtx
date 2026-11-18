#include <QtTest>
#include <QBuffer>
#include <QPointer>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/make_shared.hpp>

#include <sstream>

#include "Audio/soundout.h"

namespace
{
  class StartupLogCapture
  {
    using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;

  public:
    StartupLogCapture ()
      : m_stream {boost::make_shared<std::ostringstream> ()}
      , m_sink {new Sink}
    {
      m_sink->locked_backend ()->add_stream (m_stream);
      m_sink->set_formatter (boost::log::expressions::stream << boost::log::expressions::smessage);
      boost::log::core::get ()->add_sink (m_sink);
    }
    ~StartupLogCapture () {boost::log::core::get ()->remove_sink (m_sink);}
    QString text () const {return QString::fromStdString (m_stream->str ());}

  private:
    boost::shared_ptr<std::ostringstream> m_stream;
    boost::shared_ptr<Sink> m_sink;
  };
}

class TestSoundOutput : public QObject
{
  Q_OBJECT

  QAudioDeviceInfo device (QString const& scenario)
  {
    auto const name = QStringLiteral ("WSJTX test ") + scenario;
    for (auto const& candidate : QAudioDeviceInfo::availableDevices (QAudio::AudioOutput))
      if (candidate.deviceName () == name) return candidate;
    return {};
  }

  TxEvidence::TxPlaybackContext context (qint64 value = 1)
  {
    return {TxEvidence::TxSessionId {value}, TxEvidence::TxGeneration {value}};
  }

private Q_SLOTS:
  void initTestCase ()
  {
    QCoreApplication::addLibraryPath (QStringLiteral (SOUND_OUTPUT_TEST_PLUGIN_ROOT));
    qRegisterMetaType<TxEvidence::TxPlaybackContext> ();
    qRegisterMetaType<TxEvidence::TxOutputError> ();
    qRegisterMetaType<TxEvidence::TxRawPlayoutSnapshot> ();
    QVERIFY2 (!device ("silent-open").isNull (), "The scripted audio plugin was not loaded");
  }

  void failed_start_is_reported_before_cancellation_data ()
  {
    QTest::addColumn<QString> ("scenario");
    QTest::addColumn<int> ("error");
    for (auto scenario : {"constructor-open", "silent-open", "sync-open", "queued-open"})
      QTest::newRow (scenario) << QString::fromLatin1 (scenario)
                              << int (TxEvidence::TxBackendError::Open);
    QTest::newRow ("fatal-start") << QStringLiteral ("fatal-start")
                                 << int (TxEvidence::TxBackendError::Fatal);
  }

  void startup_log_precedes_callbacks_and_retains_attempt_data ()
  {
    QTest::addColumn<QString> ("scenario");
    QTest::addColumn<QString> ("phase");
    QTest::newRow ("configuration") << QString {} << QStringLiteral ("configuration");
    QTest::newRow ("construction") << QStringLiteral ("constructor-open") << QStringLiteral ("construction");
    QTest::newRow ("silent-start") << QStringLiteral ("silent-open") << QStringLiteral ("start");
    QTest::newRow ("synchronous-start") << QStringLiteral ("sync-open") << QStringLiteral ("start");
    QTest::newRow ("queued-start") << QStringLiteral ("queued-open") << QStringLiteral ("start");
  }

  void startup_log_precedes_callbacks_and_retains_attempt ()
  {
    QFETCH (QString, scenario);
    QFETCH (QString, phase);
    StartupLogCapture capture;
    SoundOutput output;
    output.setFormat (scenario.isEmpty () ? QAudioDeviceInfo {} : device (scenario), 2, 128);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    bool loggedBeforeSnapshot {false};
    bool loggedBeforeError {false};
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, this,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
      if (snapshot.start_event && snapshot.context == context (41))
        loggedBeforeSnapshot = capture.text ().count ("Audio output startup failed") == 1;
    });
    QSignalSpy errors {&output, &AudioOutputSink::error};
    connect (&output, &AudioOutputSink::error, this,
             [&] (TxEvidence::TxOutputError const& error) {
      if (error.context != context (41)) return;
      loggedBeforeError = capture.text ().count ("Audio output startup failed") == 1;
      output.setFormat (device ("active"), 1);
      output.restart (&source, context (42));
    });

    output.restart (&source, context (41));
    output.reset ();
    output.stop ();
    QCoreApplication::processEvents ();

    QVERIFY (loggedBeforeSnapshot);
    QVERIFY (loggedBeforeError);
    QCOMPARE (errors.count (), 1);
    auto const text = capture.text ();
    QCOMPARE (text.count ("Audio output startup failed"), 1);
    QVERIFY (text.contains ("phase=" + phase));
    QVERIFY (text.contains ("backend=qt-audio qt="));
    QVERIFY (text.contains (" platform="));
    QVERIFY (text.contains (" os_version="));
    QVERIFY (text.contains (" buffer_frames=128"));
    QVERIFY (text.contains (" state=" + QString::number (int (QAudio::StoppedState))));
    QVERIFY (text.contains (" error=" + QString::number (int (QAudio::OpenError))));
    QVERIFY (text.contains (" session=41 generation=41 backend_start_sequence=1"));
    QVERIFY (!text.contains ("session=42"));
    if (!scenario.isEmpty ())
      {
        QVERIFY (text.contains ("device=\"WSJTX test " + scenario + '"'));
        QVERIFY (text.contains (" rate_hz=48000 channels=2 sample_bits=16"));
        QVERIFY (text.contains (" sample_type=" + QString::number (int (QAudioFormat::SignedInt))));
        QVERIFY (text.contains (" byte_order="));
        QVERIFY (text.contains (" buffer_bytes=512"));
      }
  }

  void failed_start_is_reported_before_cancellation ()
  {
    QFETCH (QString, scenario);
    QFETCH (int, error);
    SoundOutput output;
    output.setFormat (device (scenario), 1);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    QSignalSpy errors {&output, &AudioOutputSink::error};
    QSignalSpy active {&output, &AudioOutputSink::audioOutputActive};
    QList<TxEvidence::TxRawPlayoutSnapshot> snapshots;
    bool failureSnapshotBeforeError {false};
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, this,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
      snapshots.append (snapshot);
      if (snapshot.start_event) failureSnapshotBeforeError = errors.isEmpty ();
    });

    output.restart (&source, context ());

    QCOMPARE (errors.count (), 1);
    QVERIFY (failureSnapshotBeforeError);
    QCOMPARE (active.count (), 0);
    auto const failure = qvariant_cast<TxEvidence::TxOutputError> (errors.front ().front ());
    QCOMPARE (failure.context, context ());
    QCOMPARE (int (failure.error), error);
    int starts {0};
    for (auto const& snapshot : snapshots)
      if (snapshot.start_event)
        {
          ++starts;
          QCOMPARE (snapshot.context, context ());
          QCOMPARE (snapshot.backend_start_sequence, qint64 {1});
          QCOMPARE (snapshot.state, TxEvidence::TxBackendState::Stopped);
          QCOMPARE (int (snapshot.error), error);
          QCOMPARE (snapshot.tier, TxEvidence::TxPlayoutTier::Unavailable);
          QVERIFY (!snapshot.available);
          QCOMPARE (snapshot.available_bytes, qint64 {-1});
          QCOMPARE (snapshot.buffered_bytes, qint64 {-1});
          QCOMPARE (snapshot.capacity_bytes, qint64 {-1});
          QCOMPARE (snapshot.processed_usecs, qint64 {-1});
          QCOMPARE (snapshot.elapsed_usecs, qint64 {-1});
        }
    QCOMPARE (starts, 1);
    QCOMPARE (output.bufferSize (), 0);
    QCOMPARE (output.unplayedBufferBytes (), 0);

    QCoreApplication::processEvents ();
    output.reset ();
    output.stop ();
    QCoreApplication::processEvents ();
    QCOMPARE (errors.count (), 1);

    output.restart (&source, context ());
    QCOMPARE (errors.count (), 2);
    QCOMPARE (qvariant_cast<TxEvidence::TxOutputError> (errors.last ().front ()).context,
              context ());
    QCoreApplication::processEvents ();
    QCOMPARE (errors.count (), 2);
  }

  void playback_metrics_follow_backend_state_data ()
  {
    QTest::addColumn<QString> ("scenario");
    QTest::addColumn<int> ("state");
    QTest::newRow ("active") << QStringLiteral ("active")
                             << int (TxEvidence::TxBackendState::Active);
    QTest::newRow ("idle") << QStringLiteral ("idle")
                           << int (TxEvidence::TxBackendState::Idle);
  }

  void startup_failure_survives_reentrant_error_clearing_data ()
  {
    QTest::addColumn<QString> ("scenario");
    QTest::addColumn<bool> ("resetFromStatus");
    QTest::newRow ("failed-snapshot-reset") << QStringLiteral ("clear-reset-open") << false;
    QTest::newRow ("synchronous-status-reset") << QStringLiteral ("sync-clear-reset-open") << true;
    QTest::newRow ("sticky-error-snapshot-reset") << QStringLiteral ("silent-open") << false;
    QTest::newRow ("sticky-construction-snapshot-reset") << QStringLiteral ("constructor-open") << false;
  }

  void startup_failure_survives_reentrant_error_clearing ()
  {
    QFETCH (QString, scenario);
    QFETCH (bool, resetFromStatus);
    SoundOutput output;
    output.setFormat (device (scenario), 1);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    bool reset {false};
    auto resetOnce = [&] {
      if (reset) return;
      reset = true;
      output.reset ();
    };
    if (resetFromStatus)
      connect (&output, &AudioOutputSink::status, this, [&] (QString const&) {resetOnce ();});
    else
      connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, this,
               [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
        if (snapshot.start_event) resetOnce ();
      });
    QSignalSpy errors {&output, &AudioOutputSink::error};
    QSignalSpy snapshots {&output, &AudioOutputSink::rawTxPlayoutSnapshot};

    output.restart (&source, context ());

    QVERIFY (reset);
    QCOMPARE (output.m_stream->error (), scenario.contains ("clear-reset") ? QAudio::NoError : QAudio::OpenError);
    QCOMPARE (errors.count (), 1);
    auto const error = qvariant_cast<TxEvidence::TxOutputError> (errors.front ().front ());
    QCOMPARE (error.context, context ());
    QCOMPARE (error.error, TxEvidence::TxBackendError::Open);
    bool preservedFailedStart {false};
    for (auto const& event : snapshots)
      {
        auto const snapshot = qvariant_cast<TxEvidence::TxRawPlayoutSnapshot> (event.front ());
        if (!snapshot.start_event) continue;
        preservedFailedStart = true;
        QCOMPARE (snapshot.context, context ());
        QCOMPARE (snapshot.backend_start_sequence, qint64 {1});
        QCOMPARE (snapshot.error, TxEvidence::TxBackendError::Open);
        QVERIFY (!snapshot.available);
      }
    QVERIFY (preservedFailedStart);
    output.reset ();
    QCoreApplication::processEvents ();
    QCOMPARE (errors.count (), 1);
  }

  void playback_metrics_follow_backend_state ()
  {
    QFETCH (QString, scenario);
    QFETCH (int, state);
    SoundOutput output;
    output.setFormat (device (scenario), 2, 256);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    QSignalSpy errors {&output, &AudioOutputSink::error};
    QList<TxEvidence::TxRawPlayoutSnapshot> snapshots;
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, this,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {snapshots.append (snapshot);});
    output.restart (&source, context ());
    QVERIFY (!snapshots.isEmpty ());
    auto const started = snapshots.last ();
    QVERIFY (started.start_event);
    QVERIFY (started.available);
    QCOMPARE (int (started.state), state);
    QCOMPARE (started.error, TxEvidence::TxBackendError::None);
    QCOMPARE (started.channel_count, 2);
    QCOMPARE (started.capacity_bytes, qint64 {1024});
    QCOMPARE (started.processed_usecs, qint64 {250000});
    QCOMPARE (output.bufferSize (), 1024);

    auto const snapshotsBeforeSuspend = snapshots.size ();
    output.suspend ();
    if (scenario == "active")
      {
        QCOMPARE (output.bufferSize (), 0);
        QCOMPARE (snapshots.last ().state, TxEvidence::TxBackendState::Suspended);
        QVERIFY (!snapshots.last ().available);
        QCOMPARE (snapshots.last ().processed_usecs, qint64 {-1});
      }
    else
      {
        QCOMPARE (output.bufferSize (), 1024);
        QCOMPARE (snapshots.size (), snapshotsBeforeSuspend);
        QCOMPARE (snapshots.last ().state, TxEvidence::TxBackendState::Idle);
      }
    output.resume ();
    QCOMPARE (output.bufferSize (), 1024);
    QVERIFY (snapshots.last ().available);

    snapshots.clear ();
    output.reset ();
    QCOMPARE (output.bufferSize (), 0);
    QVERIFY (!snapshots.last ().available);
    QCOMPARE (snapshots.last ().state, TxEvidence::TxBackendState::Stopped);
    QCOMPARE (snapshots.last ().error, TxEvidence::TxBackendError::None);
    bool retainedPlayingSnapshot {false};
    for (auto const& snapshot : snapshots)
      retainedPlayingSnapshot |= snapshot.available && snapshot.processed_usecs == 250000;
    QVERIFY (retainedPlayingSnapshot);
    output.stop ();
    QCOMPARE (errors.count (), 0);
  }

  void runtime_failure_does_not_query_failed_playback ()
  {
    SoundOutput output;
    output.setFormat (device ("runtime-fatal"), 1);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    QSignalSpy errors {&output, &AudioOutputSink::error};
    QSignalSpy snapshots {&output, &AudioOutputSink::rawTxPlayoutSnapshot};
    output.restart (&source, context ());
    QCOMPARE (errors.count (), 0);
    QTRY_COMPARE (errors.count (), 1);
    auto const snapshot = qvariant_cast<TxEvidence::TxRawPlayoutSnapshot> (snapshots.last ().front ());
    QVERIFY (!snapshot.available);
    QCOMPARE (snapshot.state, TxEvidence::TxBackendState::Stopped);
    QCOMPARE (snapshot.error, TxEvidence::TxBackendError::Fatal);
    QCOMPARE (output.unplayedBufferBytes (), 0);
    output.stop ();
  }

  void failed_snapshot_callback_can_replace_the_attempt ()
  {
    SoundOutput output;
    output.setFormat (device ("silent-open"), 1);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    QList<TxEvidence::TxRawPlayoutSnapshot> snapshots;
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, this,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
      snapshots.append (snapshot);
      if (snapshot.start_event && snapshot.context == context (1))
        {
          output.setFormat (device ("active"), 1);
          output.restart (&source, context (2));
        }
    });
    QSignalSpy errors {&output, &AudioOutputSink::error};
    output.restart (&source, context (1));
    QCOMPARE (output.m_backendStartSequence, qint64 {2});
    QCOMPARE (output.m_context, context (2));
    QVERIFY (snapshots.last ().available);
    QCOMPARE (snapshots.last ().context, context (2));
    QCOMPARE (snapshots.last ().backend_start_sequence, qint64 {2});
    for (auto const& error : errors)
      QVERIFY (qvariant_cast<TxEvidence::TxOutputError> (error.front ()).context != context (2));
  }

  void error_callback_can_restart_after_failure ()
  {
    SoundOutput output;
    output.setFormat (device ("sync-open"), 1);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    QSignalSpy errors {&output, &AudioOutputSink::error};
    connect (&output, &AudioOutputSink::error, this,
             [&] (TxEvidence::TxOutputError const& error) {
      if (error.context == context (1))
        {
          output.setFormat (device ("active"), 1);
          output.restart (&source, context (2));
        }
    });
    QSignalSpy snapshots {&output, &AudioOutputSink::rawTxPlayoutSnapshot};
    output.restart (&source, context (1));
    QCOMPARE (errors.count (), 1);
    QCOMPARE (output.m_context, context (2));
    auto const last = qvariant_cast<TxEvidence::TxRawPlayoutSnapshot> (snapshots.last ().front ());
    QCOMPARE (last.context, context (2));
    QCOMPARE (last.backend_start_sequence, qint64 {2});
    QVERIFY (last.available);

    output.setFormat (device ("silent-open"), 1);
    output.restart (&source, context (3));
    QCOMPARE (errors.count (), 2);
    QCOMPARE (qvariant_cast<TxEvidence::TxOutputError> (errors.last ().front ()).context,
              context (3));
  }

  void active_callback_can_reset_before_start_returns ()
  {
    SoundOutput output;
    output.setFormat (device ("active"), 1);
    QBuffer source;
    QVERIFY (source.open (QIODevice::ReadOnly));
    connect (&output, &AudioOutputSink::audioOutputActive, &output, &SoundOutput::reset);
    QSignalSpy errors {&output, &AudioOutputSink::error};
    QSignalSpy snapshots {&output, &AudioOutputSink::rawTxPlayoutSnapshot};
    output.restart (&source, context ());
    QCOMPARE (errors.count (), 0);
    QCOMPARE (output.bufferSize (), 0);
    auto const last = qvariant_cast<TxEvidence::TxRawPlayoutSnapshot> (snapshots.last ().front ());
    QCOMPARE (last.state, TxEvidence::TxBackendState::Stopped);
    QVERIFY (!last.available);
  }

  void failed_reconfiguration_retires_previous_stream ()
  {
    SoundOutput output;
    QAudioFormat format;
    format.setCodec ("audio/pcm");
    format.setSampleRate (48000);
    format.setSampleSize (16);
    format.setSampleType (QAudioFormat::SignedInt);
    format.setChannelCount (1);
    format.setByteOrder (QAudioFormat::LittleEndian);
    QVERIFY (format.isValid ());
    output.m_stream.reset (new QAudioOutput (QAudioDeviceInfo {}, format));
    QPointer<QAudioOutput> previousStream {output.m_stream.data ()};
    output.setFormat (QAudioDeviceInfo {}, 1);

    QSignalSpy active {&output, &AudioOutputSink::audioOutputActive};
    QSignalSpy errors {&output, &AudioOutputSink::error};
    QList<TxEvidence::TxRawPlayoutSnapshot> snapshots;
    bool retiredBeforeNotification {true};
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, this,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
               retiredBeforeNotification &= previousStream.isNull ();
               snapshots.append (snapshot);
             });
    connect (&output, &AudioOutputSink::error, this, [&] (TxEvidence::TxOutputError const&) {
      retiredBeforeNotification &= previousStream.isNull ();
    });
    QBuffer source;
    source.setData (QByteArray (1024, '\0'));
    QVERIFY (source.open (QIODevice::ReadOnly));

    TxEvidence::TxPlaybackContext const contextA {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
    TxEvidence::TxPlaybackContext const contextB {TxEvidence::TxSessionId {102}, TxEvidence::TxGeneration {202}};
    output.restart (&source, contextA);

    QVERIFY (previousStream.isNull ());
    QVERIFY (retiredBeforeNotification);
    QCOMPARE (output.bufferSize (), 0);
    QCOMPARE (active.count (), 0);
    QCOMPARE (errors.count (), 1);
    QCOMPARE (source.pos (), qint64 {0});
    QCOMPARE (snapshots.size (), 1);
    QVERIFY (snapshots.front ().start_event);
    QVERIFY (!snapshots.front ().available);
    QCOMPARE (snapshots.front ().state, TxEvidence::TxBackendState::Unavailable);
    QCOMPARE (snapshots.front ().error, TxEvidence::TxBackendError::Open);
    QCOMPARE (snapshots.front ().backend_start_sequence, qint64 {1});
    QCOMPARE (snapshots.front ().context, contextA);
    auto const firstError = qvariant_cast<TxEvidence::TxOutputError> (errors.front ().front ());
    QCOMPARE (firstError.context, contextA);
    QCOMPARE (firstError.error, TxEvidence::TxBackendError::Open);

    output.restart (&source, contextB);
    QCOMPARE (output.bufferSize (), 0);
    QCOMPARE (active.count (), 0);
    QCOMPARE (errors.count (), 2);
    QCOMPARE (snapshots.size (), 2);
    QVERIFY (!snapshots.back ().available);
    QCOMPARE (snapshots.back ().backend_start_sequence, qint64 {2});
    QCOMPARE (snapshots.back ().context, contextB);
    auto const secondError = qvariant_cast<TxEvidence::TxOutputError> (errors.back ().front ());
    QCOMPARE (secondError.context, contextB);
    QCOMPARE (secondError.error, TxEvidence::TxBackendError::Open);
  }

  void queued_snapshots_and_errors_retain_their_original_context ()
  {
    SoundOutput output;
    output.setFormat (QAudioDeviceInfo {}, 1);
    QList<TxEvidence::TxRawPlayoutSnapshot> snapshots;
    QList<TxEvidence::TxOutputError> errors;
    QObject receiver;
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, &receiver,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
               snapshots.append (snapshot);
             }, Qt::QueuedConnection);
    connect (&output, &AudioOutputSink::error, &receiver,
             [&] (TxEvidence::TxOutputError const& failure) {
               errors.append (failure);
             }, Qt::QueuedConnection);
    TxEvidence::TxPlaybackContext const contextA {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
    TxEvidence::TxPlaybackContext const contextB {TxEvidence::TxSessionId {102}, TxEvidence::TxGeneration {202}};
    output.restart (nullptr, contextA);
    output.stop ();
    output.restart (nullptr, contextB);
    output.reset ();
    QVERIFY (snapshots.isEmpty ());
    QVERIFY (errors.isEmpty ());

    QCoreApplication::sendPostedEvents (&receiver, QEvent::MetaCall);

    QCOMPARE (snapshots.size (), 4);
    QCOMPARE (errors.size (), 2);
    QCOMPARE (snapshots.at (0).context, contextA);
    QCOMPARE (snapshots.at (1).context, contextA);
    QCOMPARE (snapshots.at (2).context, contextB);
    QCOMPARE (snapshots.at (3).context, contextB);
    QVERIFY (snapshots.at (0).start_event);
    QVERIFY (!snapshots.at (1).start_event);
    QCOMPARE (errors.at (0).context, contextA);
    QCOMPARE (errors.at (1).context, contextB);
  }

  void stale_backend_callbacks_cannot_activate_a_replacement ()
  {
    SoundOutput output;
    output.setFormat (QAudioDeviceInfo {}, 1);
    TxEvidence::TxPlaybackContext const context {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
    output.restart (nullptr, context);
    auto const oldSequence = output.m_backendStartSequence;
    QSignalSpy active {&output, &AudioOutputSink::audioOutputActive};
    QSignalSpy snapshots {&output, &AudioOutputSink::rawTxPlayoutSnapshot};
    QVERIFY (QMetaObject::invokeMethod (&output, [&] {
      output.handleStateChanged (QAudio::ActiveState, context, oldSequence);
    }, Qt::QueuedConnection));
    output.restart (nullptr, context);
    QAudioFormat format;
    format.setCodec ("audio/pcm");
    format.setSampleRate (48000);
    format.setSampleSize (16);
    format.setSampleType (QAudioFormat::SignedInt);
    format.setChannelCount (1);
    output.m_stream.reset (new QAudioOutput (QAudioDeviceInfo {}, format));
    snapshots.clear ();

    QCoreApplication::sendPostedEvents (&output, QEvent::MetaCall);

    QCOMPARE (active.count (), 0);
    QCOMPARE (snapshots.count (), 0);
  }
};

QTEST_GUILESS_MAIN (TestSoundOutput)
#include "test_sound_output.moc"
