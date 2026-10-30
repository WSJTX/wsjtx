#include <QtTest>

#include <atomic>
#include <memory>

#include <QDir>
#include <QHostAddress>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "Transceiver/HRDMessage.hpp"
#include "Transceiver/HRDTransceiver.hpp"

namespace
{
  class ScriptedHRDServer final
    : public QThread
  {
  public:
    enum class Profile
    {
      normal,
      mixed_radios,
      missing_current_radio,
      malformed_radios,
      malformed_frequencies,
      missing_split_on,
      missing_split_off,
      unknown_mode,
      wrong_dropdown_name,
      modes_only_cw
    };

    explicit ScriptedHRDServer (Profile profile)
      : profile_ {profile}
    {
    }

    ~ScriptedHRDServer () override
    {
      stop ();
    }

    bool start_server ()
    {
      start ();
      return ready_.tryAcquire (1, 5000) && port_;
    }

    void stop ()
    {
      stop_.store (true);
      wait (5000);
    }

    quint16 port () const
    {
      return port_;
    }

    QString error () const
    {
      QMutexLocker lock {&mutex_};
      return error_;
    }

    QStringList commands () const
    {
      QMutexLocker lock {&mutex_};
      return commands_;
    }

    void set_scalar_replies (QStringList replies, QString vfo_count = "1")
    {
      QMutexLocker lock {&config_mutex_};
      scalar_replies_ = std::move (replies);
      vfo_count_reply_ = vfo_count;
    }

    void set_split_replies (QString selections, QStringList replies)
    {
      QMutexLocker lock {&config_mutex_};
      split_selections_ = selections;
      split_replies_ = std::move (replies);
    }

    void enable_vfo_buttons ()
    {
      QMutexLocker lock {&config_mutex_};
      vfo_buttons_ = true;
    }

  protected:
    void run () override
    {
      QTcpServer server;
      if (!server.listen (QHostAddress {QStringLiteral ("127.0.0.1")}, 0))
        {
          set_error (server.errorString ());
          ready_.release ();
          return;
        }
      port_ = server.serverPort ();
      ready_.release ();

      while (!stop_.load ())
        {
          if (!server.waitForNewConnection (100)) continue;
          std::unique_ptr<QTcpSocket> peer {server.nextPendingConnection ()};
          serve (peer.get ());
        }
    }

  private:
    void serve (QTcpSocket * peer)
    {
      QByteArray input;
      while (!stop_.load ())
        {
          auto const parsed = HRDMessage::parse (input);
          if (!parsed.valid)
            {
              set_error ("client sent an invalid HRD frame: " + parsed.error);
              return;
            }
          if (!parsed.complete)
            {
              if (peer->waitForReadyRead (100))
                {
                  input += peer->readAll ();
                  continue;
                }
              if (QAbstractSocket::UnconnectedState == peer->state ())
                {
                  return;
                }
              continue;
            }

          input.remove (0, parsed.required_size);
          record_command (parsed.payload);
          auto const command = without_context (parsed.payload);
          auto const reply = reply_for (command);
          if (!write_reply (peer, reply, "get version" == command))
            {
              return;
            }
        }
    }

    static QString without_context (QString command)
    {
      if (command.startsWith ('['))
        {
          auto const context_end = command.indexOf ("] ");
          if (context_end >= 0)
            {
              command = command.mid (context_end + 2);
            }
        }
      return command;
    }

    static QString next_reply (QStringList& replies)
    {
      return replies.size () > 1 ? replies.takeFirst () : replies.front ();
    }

    QString reply_for (QString const& command)
    {
      QMutexLocker lock {&config_mutex_};
      if ("get context" == command) return "OK";
      if ("get id" == command) return "HRD";
      if ("get version" == command) return "6.8.0";
      if ("get radios" == command)
        {
          if (Profile::mixed_radios == profile_) return "junk, 2:Backup,, 1:FTDX-3000 ,:bad";
          if (Profile::missing_current_radio == profile_) return "junk,2:Backup";
          return Profile::malformed_radios == profile_ ? "malformed" : "1:FTDX-3000";
        }
      if ("get radio" == command) return "FTDX-3000";
      if ("get vfo-count" == command) return vfo_count_reply_;
      if ("get buttons" == command) return vfo_buttons_ ? "VFO A,VFO B" : QString {};
      if (vfo_buttons_ && command.startsWith ("set button-select VFO~")) return "OK";
      if (vfo_buttons_ && command.startsWith ("get button-select VFO~")) return "0";
      if (command.startsWith ("set frequency-hz ")) return "OK";
      if ("get dropdowns" == command)
        {
          if (!split_selections_.isNull ()) return "Split";
          if (Profile::missing_split_on == profile_
              || Profile::missing_split_off == profile_) return "Main Mode,Split";
          if (Profile::modes_only_cw == profile_
              || Profile::unknown_mode == profile_
              || Profile::wrong_dropdown_name == profile_) return "Main Mode";
          return {};
        }
      if ("get dropdown-list {Main Mode}" == command)
        {
          return Profile::unknown_mode == profile_ ? "CW,Custom" : "CW";
        }
      if ("get dropdown-list {Split}" == command)
        {
          if (!split_selections_.isNull ()) return split_selections_;
          return Profile::missing_split_off == profile_ ? "On" : "Off";
        }
      if ("get sliders" == command) return {};
      if ("get frequency" == command) return next_reply (scalar_replies_);
      if ("get frequencies" == command)
        {
          return Profile::malformed_frequencies == profile_
            ? "14074001" : "10368100000-7074001";
        }
      if ("get dropdown-text {Main Mode}" == command)
        {
          if (Profile::unknown_mode == profile_) return "Main Mode:Custom";
          if (Profile::wrong_dropdown_name == profile_) return "Wrong Mode:CW";
          return "Main Mode:CW";
        }
      if ("get dropdown-text {Split}" == command)
        {
          if (!split_replies_.isEmpty ()) return next_reply (split_replies_);
          return Profile::missing_split_off == profile_ ? "Split:On" : "Split:Off";
        }

      set_error ("unexpected command: " + command);
      return {};
    }

    bool write_reply (QTcpSocket * peer, QString const& reply, bool fragmented)
    {
      auto const frame = HRDMessage::make (reply);
      if (fragmented)
        {
          auto const split = frame.size () / 2;
          if (!write (peer, frame.left (split))) return false;
          QThread::msleep (10);
          return write (peer, frame.mid (split));
        }
      return write (peer, frame);
    }

    bool write (QTcpSocket * peer, QByteArray const& bytes)
    {
      if (peer->write (bytes) != bytes.size () || !peer->waitForBytesWritten (1000))
        {
          set_error (peer->errorString ());
          return false;
        }
      return true;
    }

    void record_command (QString const& command)
    {
      QMutexLocker lock {&mutex_};
      commands_.append (command);
    }

    void set_error (QString const& message)
    {
      QMutexLocker lock {&mutex_};
      if (error_.isEmpty ()) error_ = message;
    }

    QMutex config_mutex_;
    QStringList scalar_replies_ {"14074001"};
    QString vfo_count_reply_ {"2"};
    QString split_selections_;
    QStringList split_replies_;
    bool vfo_buttons_ {false};
    Profile profile_;
    std::atomic_bool stop_ {false};
    QSemaphore ready_;
    quint16 port_ {0};
    mutable QMutex mutex_;
    QStringList commands_;
    QString error_;
  };

  struct HRDFixture
  {
    explicit HRDFixture (ScriptedHRDServer::Profile profile)
      : server {profile}
    {
    }

    ~HRDFixture ()
    {
      if (transceiver) transceiver->stop ();
      transceiver.reset ();
      server.stop ();
    }

    bool start ()
    {
      if (!server.start_server ()) return false;
      auto const address = QString {"127.0.0.1:%1"}.arg (server.port ());
      transceiver.reset (new HRDTransceiver {
          &logger, {}, address, true, TransceiverFactory::TX_audio_source_front, 0});
      QObject::connect (transceiver.get (), &Transceiver::failure,
                        [&] (QString const& message) { failure = message; });
      QObject::connect (transceiver.get (), &Transceiver::update,
                        [&] (Transceiver::TransceiverState const& value, unsigned)
                        {
                          state = value;
                        });
      QObject::connect (transceiver.get (), &Transceiver::resolution,
                        [&] (int value) { resolution = value; });
      transceiver->start (1);
      return true;
    }

    ScriptedHRDServer server;
    Transceiver::logger_type logger;
    std::unique_ptr<HRDTransceiver> transceiver;
    QString failure;
    int resolution {99};
    Transceiver::TransceiverState state;
  };
}

class TestHRDTransceiver final
  : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase ()
  {
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setOrganizationName ("WSJT-X tests");
    QCoreApplication::setApplicationName ("HRD transceiver");
    QVERIFY (QDir {}.mkpath (
        QStandardPaths::writableLocation (QStandardPaths::DataLocation)));
  }

  void exchangesFramedStartupAndPollCommands_data ()
  {
    QTest::addColumn<int> ("profile");
    QTest::newRow ("normal") << int (ScriptedHRDServer::Profile::normal);
    QTest::newRow ("mixed-radios") << int (ScriptedHRDServer::Profile::mixed_radios);
  }

  void exchangesFramedStartupAndPollCommands ()
  {
    QFETCH (int, profile);
    HRDFixture fixture {static_cast<ScriptedHRDServer::Profile> (profile)};
    QVERIFY2 (fixture.start (), qPrintable (fixture.server.error ()));
    QVERIFY (fixture.failure.isEmpty ());

    QTRY_COMPARE_WITH_TIMEOUT (
        fixture.state.frequency (), quint64 (10368100000), 2500);
    QVERIFY (fixture.failure.isEmpty ());
    QVERIFY (fixture.state.online ());
    QCOMPARE (fixture.state.tx_frequency (), quint64 (7074001));
    auto const commands = fixture.server.commands ();
    QCOMPARE (commands.value (0), QString {"get context"});
    QVERIFY (commands.contains ("[1] get vfo-count"));
    QVERIFY (commands.contains ("[1] get frequencies"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void rejectsMalformedRadioListDuringStartup ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::malformed_radios};
    QVERIFY2 (fixture.start (), qPrintable (fixture.server.error ()));

    QVERIFY (fixture.failure.contains ("didn't respond as expected"));
    QVERIFY (fixture.server.commands ().contains ("get radios"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void rejectsMalformedFrequenciesDuringPolling ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::malformed_frequencies};
    QVERIFY2 (fixture.start (), qPrintable (fixture.server.error ()));
    QVERIFY (fixture.failure.isEmpty ());

    QTRY_VERIFY_WITH_TIMEOUT (!fixture.failure.isEmpty (), 1500);
    QVERIFY (fixture.failure.contains ("didn't respond as expected"));
    QVERIFY (fixture.server.commands ().contains ("[1] get frequencies"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void rejectsMissingCurrentRadio ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::missing_current_radio};
    QVERIFY2 (fixture.start (), qPrintable (fixture.server.error ()));
    QVERIFY (fixture.failure.contains ("rig has disappeared or changed"));
    QVERIFY (!fixture.server.commands ().contains ("[2] get vfo-count"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void toleratesIncompleteDropdowns_data ()
  {
    QTest::addColumn<int> ("profile");
    QTest::addColumn<int> ("mode");
    QTest::newRow ("missing-split-on")
      << int (ScriptedHRDServer::Profile::missing_split_on) << int (Transceiver::CW);
    QTest::newRow ("missing-split-off")
      << int (ScriptedHRDServer::Profile::missing_split_off) << int (Transceiver::CW);
    QTest::newRow ("unknown-mode")
      << int (ScriptedHRDServer::Profile::unknown_mode) << int (Transceiver::UNK);
    QTest::newRow ("wrong-dropdown-name")
      << int (ScriptedHRDServer::Profile::wrong_dropdown_name) << int (Transceiver::UNK);
  }

  void toleratesIncompleteDropdowns ()
  {
    QFETCH (int, profile);
    QFETCH (int, mode);
    HRDFixture fixture {static_cast<ScriptedHRDServer::Profile> (profile)};
    QVERIFY2 (fixture.start (), qPrintable (fixture.server.error ()));
    QVERIFY (fixture.failure.isEmpty ());

    QTRY_COMPARE_WITH_TIMEOUT (fixture.state.frequency (), quint64 (10368100000), 2500);
    QVERIFY (fixture.server.commands ().contains ("[1] get dropdown-text {Main Mode}"));
    QVERIFY (fixture.failure.isEmpty ());
    QCOMPARE (int (fixture.state.mode ()), mode);
    QVERIFY (!fixture.state.split ());
    QVERIFY (fixture.state.online ());
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void validatesRequiredNumericReplies_data ()
  {
    QTest::addColumn<QString> ("frequency");
    QTest::addColumn<QString> ("vfoCount");
    QTest::newRow ("empty-frequency") << QString {} << "2";
    QTest::newRow ("malformed-frequency") << "14074001Hz" << "2";
    QTest::newRow ("negative-frequency") << "-1" << "2";
    QTest::newRow ("overflow-frequency") << "18446744073709551616" << "2";
    QTest::newRow ("empty-vfo-count") << "14074001" << QString {};
    QTest::newRow ("malformed-vfo-count") << "14074001" << "two";
    QTest::newRow ("zero-vfo-count") << "14074001" << "0";
    QTest::newRow ("negative-vfo-count") << "14074001" << "-1";
    QTest::newRow ("overflow-vfo-count") << "14074001" << "4294967296";
  }

  void validatesRequiredNumericReplies ()
  {
    QFETCH (QString, frequency);
    QFETCH (QString, vfoCount);
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    fixture.server.set_scalar_replies ({frequency}, vfoCount);
    QVERIFY (fixture.start ());
    QVERIFY (fixture.failure.contains ("didn't respond as expected"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void pollsSingleVfoWith64BitFrequencies ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    fixture.server.enable_vfo_buttons ();
    fixture.server.set_scalar_replies ({"10368100001", "7074001", "10368100001"});
    QVERIFY (fixture.start ());
    QVERIFY (fixture.failure.isEmpty ());
    QTRY_COMPARE_WITH_TIMEOUT (fixture.state.frequency (), quint64 (10368100001), 2500);
    QVERIFY (fixture.server.commands ().contains ("[1] set button-select VFO~B 1"));
    fixture.server.set_scalar_replies ({" 10368100003 "});
    QTRY_COMPARE_WITH_TIMEOUT (fixture.state.frequency (), quint64 (10368100003), 1500);
    fixture.server.set_scalar_replies ({"0"});
    auto const before = fixture.server.commands ().count ("[1] get frequency");
    QTRY_VERIFY_WITH_TIMEOUT (fixture.server.commands ().count ("[1] get frequency") > before, 1500);
    QCOMPARE (fixture.state.frequency (), quint64 (10368100003));
    QVERIFY (fixture.failure.isEmpty ());
    fixture.server.set_scalar_replies ({"invalid"});
    QTRY_VERIFY_WITH_TIMEOUT (!fixture.failure.isEmpty (), 1500);
    QVERIFY (fixture.failure.contains ("didn't respond as expected"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void probesResolutionAndRestoresFrequency_data ()
  {
    QTest::addColumn<QString> ("original");
    QTest::addColumn<QStringList> ("readback");
    QTest::addColumn<int> ("resolution");
    QTest::newRow ("ten-hz-truncated") << "10368100000" << QStringList {"10368100050"} << -1;
    QTest::newRow ("twenty-hz-truncated") << "10368100000" << QStringList {"10368100040"} << -2;
    QTest::newRow ("hundred-hz-truncated") << "10368100000" << QStringList {"10368100000"} << -2;
    QTest::newRow ("hundred-hz-rounded") << "10368100000" << QStringList {"10368100100"} << 2;
    QTest::newRow ("ten-hz-rounded") << "10368100000" << QStringList {"10368100060", "10368100050"} << 1;
    QTest::newRow ("twenty-hz-rounded") << "10368100000" << QStringList {"10368100060", "10368100060"} << 2;
    QTest::newRow ("large-negative-delta") << "10368100000" << QStringList {"1"} << 0;
    QTest::newRow ("large-positive-delta") << "10368100000" << QStringList {"18446744073709551615"} << 0;
    QTest::newRow ("large-second-delta") << "10368100000" << QStringList {"10368100060", "18446744073709551615"} << 0;
    QTest::newRow ("probe-would-overflow") << "18446744073709551610" << QStringList {} << 0;
    QTest::newRow ("zero-is-valid") << "0" << QStringList {} << 0;
  }

  void probesResolutionAndRestoresFrequency ()
  {
    QFETCH (QString, original);
    QFETCH (QStringList, readback);
    QFETCH (int, resolution);
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    fixture.server.set_scalar_replies (QStringList {original} + readback + QStringList {original}, "3");
    QVERIFY (fixture.start ());
    QVERIFY (fixture.failure.isEmpty ());
    QCOMPARE (fixture.resolution, resolution);
    auto const commands = fixture.server.commands ();
    auto const writes = commands.filter ("set frequency-hz");
    if (readback.isEmpty ())
      {
        QVERIFY (writes.isEmpty ());
      }
    else
      {
        QCOMPARE (writes.size (), readback.size () + 1);
        QCOMPARE (writes.front (), QString {"[1] set frequency-hz 10368100055"});
        QCOMPARE (writes.back (), "[1] set frequency-hz " + original);
      }
    QTRY_COMPARE_WITH_TIMEOUT (fixture.state.frequency (), quint64 (10368100000), 2500);
    QVERIFY (fixture.server.commands ().contains ("[1] get frequencies"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void restoresFrequencyAfterMalformedProbe_data ()
  {
    QTest::addColumn<QStringList> ("readback");
    QTest::newRow ("first-probe") << QStringList {"10368100000", "invalid"};
    QTest::newRow ("second-probe") << QStringList {"10368100000", "10368100060", "invalid"};
  }

  void restoresFrequencyAfterMalformedProbe ()
  {
    QFETCH (QStringList, readback);
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    fixture.server.set_scalar_replies (readback);
    QVERIFY (fixture.start ());
    QVERIFY (fixture.failure.contains ("didn't respond as expected"));
    auto const writes = fixture.server.commands ().filter ("set frequency-hz");
    QCOMPARE (writes.size (), readback.size ());
    QCOMPARE (writes.back (), QString {"[1] set frequency-hz 10368100000"});
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void retriesInvalidSplitReadback ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    fixture.server.set_split_replies ("Off,On", {" Split : On "});
    QVERIFY (fixture.start ());
    QTRY_VERIFY_WITH_TIMEOUT (fixture.state.split (), 2500);
    // Each poll queries once for tracing and again to update state.
    fixture.server.set_split_replies ("Off,On", {"Split:Off", "Wrong:Off"});
    auto const before = fixture.server.commands ().count ("[1] get dropdown-text {Split}");
    QTRY_VERIFY_WITH_TIMEOUT (fixture.server.commands ().count ("[1] get dropdown-text {Split}") >= before + 2, 1500);
    QVERIFY (fixture.state.split ());
    QVERIFY (fixture.state.online ());
    QVERIFY (fixture.failure.isEmpty ());
    fixture.server.set_split_replies ("Off,On", {"Split:Off"});
    QTRY_VERIFY_WITH_TIMEOUT (!fixture.state.split (), 1500);
    QVERIFY (fixture.state.online ());
    QVERIFY (fixture.failure.isEmpty ());
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void recomputesSplitCapabilityOnRestart ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    fixture.server.set_split_replies ("Off", {"Split:Off"});
    QVERIFY (fixture.start ());
    QTRY_COMPARE_WITH_TIMEOUT (fixture.state.frequency (), quint64 (10368100000), 2500);
    QVERIFY (!fixture.server.commands ().contains ("[1] get dropdown-text {Split}"));
    fixture.transceiver->stop ();
    fixture.server.set_split_replies ("Off,On", {"Split:On"});
    fixture.transceiver->start (2);
    QTRY_VERIFY_WITH_TIMEOUT (fixture.state.split (), 2500);
    fixture.transceiver->stop ();
    fixture.server.set_split_replies ("On", {"Split:On"});
    auto const before = fixture.server.commands ().count ("[1] get dropdown-text {Split}");
    fixture.transceiver->start (3);
    QTRY_VERIFY_WITH_TIMEOUT (!fixture.state.split (), 2500);
    QCOMPARE (fixture.server.commands ().count ("[1] get dropdown-text {Split}"), before);
    QVERIFY (fixture.state.online ());
    QVERIFY (fixture.failure.isEmpty ());
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void rejectsRequestedModeMissingFromDropdown ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::modes_only_cw};
    QVERIFY2 (fixture.start (), qPrintable (fixture.server.error ()));
    QVERIFY (fixture.failure.isEmpty ());

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.frequency (14074001);
    requested.mode (Transceiver::USB);
    fixture.transceiver->set (requested, 2);

    QVERIFY (fixture.failure.contains ("rig doesn't support mode"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }
};

QTEST_GUILESS_MAIN (TestHRDTransceiver)

#include "test_hrd_transceiver.moc"
