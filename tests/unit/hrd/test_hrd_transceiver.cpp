#include <QtTest>

#include <atomic>
#include <memory>
#include <vector>

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
      bare_mode_reply,
      empty_mode_reply,
      unlisted_bare_mode_reply,
      modes_only_cw
    };

    struct Capabilities
    {
      QString radio {"FTDX-3000"};
      unsigned id {1};
      QStringList buttons;
      QStringList checked_buttons;
      QStringList dropdowns;
      QMap<QString, QStringList> selections;
      QMap<QString, QString> values;
      bool bare_replies {false};
    };

    void set_capabilities (Capabilities const& capabilities)
    {
      QMutexLocker lock {&config_mutex_};
      capabilities_ = capabilities;
      custom_capabilities_ = true;
    }

    void set_button_replies (QString const& command, QStringList replies)
    {
      QMutexLocker lock {&config_mutex_};
      button_replies_[command] = std::move (replies);
    }

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
      if (custom_capabilities_)
        {
          if ("get radios" == command) return QString::number (capabilities_.id) + ':' + capabilities_.radio;
          if ("get radio" == command) return capabilities_.radio;
          if ("get buttons" == command) return capabilities_.buttons.join (',');
          if ("get dropdowns" == command) return capabilities_.dropdowns.join (',');
          for (auto const& button : capabilities_.buttons)
            {
              auto name = button;
              name.replace (' ', '~');
              if (command == "get button-select " + name)
                return capabilities_.checked_buttons.contains (button) ? "1" : "0";
              if (command == "set button-select " + name + " 0"
                  || command == "set button-select " + name + " 1")
                {
                  if (button_replies_.contains (command)) return next_reply (button_replies_[command]);
                  return "OK";
                }
            }
          for (auto const& name : capabilities_.dropdowns)
            {
              auto const selections = capabilities_.selections.value (name);
              if (command == "get dropdown-list {" + name + '}') return selections.join (',');
              if (command == "get dropdown-text {" + name + '}')
                return (capabilities_.bare_replies ? QString {} : name + ':') + capabilities_.values.value (name);
              auto wire_name = name;
              wire_name.replace (' ', '~');
              for (int i = 0; i < selections.size (); ++i)
                {
                  auto value = selections[i];
                  value.replace (' ', '~');
                  if (command == "set dropdown " + wire_name + ' ' + value + ' ' + QString::number (i))
                    {
                      capabilities_.values[name] = selections[i];
                      return "OK";
                    }
                }
            }
          if (command.startsWith ("get button-select ") || command.startsWith ("set button-select ")
              || command.startsWith ("get dropdown-") || command.startsWith ("set dropdown "))
            {
              set_error ("unadvertised control command: " + command);
              return {};
            }
        }
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
      if (command == "set frequencies-hz 10368100000 7074001") return "OK";
      if (command == "set frequencies-hz 10368100000 7074003") return "OK";
      if ("get dropdowns" == command)
        {
          if (!split_selections_.isNull ()) return "Split";
          if (Profile::missing_split_on == profile_
              || Profile::missing_split_off == profile_) return "Main Mode,Split";
          if (Profile::modes_only_cw == profile_
              || Profile::unknown_mode == profile_
              || Profile::wrong_dropdown_name == profile_
              || Profile::bare_mode_reply == profile_
              || Profile::empty_mode_reply == profile_
              || Profile::unlisted_bare_mode_reply == profile_) return "Main Mode";
          return {};
        }
      if ("get dropdown-list {Main Mode}" == command)
        {
          if (Profile::bare_mode_reply == profile_) return "CW,USB";
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
          if (Profile::bare_mode_reply == profile_) return "USB";
          if (Profile::empty_mode_reply == profile_) return {};
          if (Profile::unlisted_bare_mode_reply == profile_) return "LSB";
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

    Capabilities capabilities_;
    bool custom_capabilities_ {false};
    QMap<QString, QStringList> button_replies_;
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

    bool poll ()
    {
      return QMetaObject::invokeMethod (transceiver.get (), "handle_timeout", Qt::DirectConnection);
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
    QTest::newRow ("bare-mode-reply")
      << int (ScriptedHRDServer::Profile::bare_mode_reply) << int (Transceiver::USB);
    QTest::newRow ("empty-mode-reply")
      << int (ScriptedHRDServer::Profile::empty_mode_reply) << int (Transceiver::UNK);
    QTest::newRow ("unlisted-bare-mode-reply")
      << int (ScriptedHRDServer::Profile::unlisted_bare_mode_reply) << int (Transceiver::UNK);
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

  void acceptsZeroVfoCount ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    fixture.server.set_scalar_replies ({"14074001"}, "0");
    QVERIFY (fixture.start ());
    QVERIFY (fixture.failure.isEmpty ());
    QTRY_COMPARE_WITH_TIMEOUT (fixture.state.frequency (), quint64 (14074001), 2500);
    QVERIFY (fixture.state.online ());
    QVERIFY (!fixture.server.commands ().contains ("[1] get frequencies"));
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
        if (readback.size () == 2)
          QCOMPARE (writes.at (1), QString {"[1] set frequency-hz 10368100051"});
        QCOMPARE (writes.back (), "[1] set frequency-hz " + original);
      }
    QElapsedTimer watchdog;
    watchdog.start ();
    while (fixture.state.frequency () != 10368100000 && fixture.failure.isEmpty () && watchdog.elapsed () < 5000)
      QVERIFY (fixture.poll ());
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (fixture.state.frequency (), quint64 (10368100000));
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

  void restoresVfoAfterMalformedReadback_data ()
  {
    QTest::addColumn<bool> ("toggle");
    QTest::addColumn<bool> ("restoreFails");
    QTest::newRow ("buttons") << false << false;
    QTest::newRow ("toggle") << true << false;
    QTest::newRow ("buttons-restore-fails") << false << true;
    QTest::newRow ("toggle-restore-fails") << true << true;
  }

  void restoresVfoAfterMalformedReadback ()
  {
    QFETCH (bool, toggle);
    QFETCH (bool, restoreFails);
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    ScriptedHRDServer::Capabilities capabilities;
    capabilities.buttons = toggle ? QStringList {"A / B"} : QStringList {"VFO A", "VFO B"};
    fixture.server.set_capabilities (capabilities);
    fixture.server.set_scalar_replies ({"10368100001", "invalid"});
    auto const away = toggle ? QString {"set button-select A~/~B 1"} : QString {"set button-select VFO~B 1"};
    auto const restore = toggle ? away : QString {"set button-select VFO~A 1"};
    if (restoreFails)
      fixture.server.set_button_replies (restore, toggle ? QStringList {"OK", "ERROR"} : QStringList {"ERROR"});
    QVERIFY (fixture.start ());
    QCOMPARE (fixture.failure, QString {"Ham Radio Deluxe didn't respond as expected"});
    fixture.transceiver->stop ();
    auto const commands = fixture.server.commands ();
    QCOMPARE (commands.count ("[1] " + away), toggle ? 2 : 1);
    QCOMPARE (commands.count ("[1] " + restore), toggle ? 2 : 1);
    auto const first_switch = commands.indexOf ("[1] " + away);
    auto const failed_read = commands.indexOf ("[1] get frequency", first_switch);
    QVERIFY (failed_read > first_switch);
    QVERIFY (commands.lastIndexOf ("[1] " + restore) > failed_read);
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void bareRepliesAreAcceptedOnlyForModeDropdowns ()
  {
    for (bool const bare : {false, true})
      {
        HRDFixture fixture {ScriptedHRDServer::Profile::normal};
        ScriptedHRDServer::Capabilities capabilities;
        capabilities.dropdowns = QStringList {"Main Mode", "Split"};
        capabilities.selections["Main Mode"] = QStringList {"CW", "USB"};
        capabilities.selections["Split"] = QStringList {"Off", "On"};
        capabilities.values["Main Mode"] = "USB";
        capabilities.values["Split"] = "On";
        capabilities.bare_replies = bare;
        fixture.server.set_capabilities (capabilities);
        QVERIFY (fixture.start ());
        QVERIFY (fixture.poll ());
        QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::USB);
        QCOMPARE (fixture.transceiver->state ().split (), !bare);
        fixture.transceiver->stop ();
        QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
      }
  }

  void ignoresModeReadbackWhileTransmitting ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    ScriptedHRDServer::Capabilities capabilities;
    capabilities.buttons = QStringList {"TX"};
    capabilities.dropdowns = QStringList {"Mode"};
    capabilities.selections["Mode"] = QStringList {"USB", "DATA-U"};
    capabilities.values["Mode"] = "DATA-U";
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.start ());
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::DIG_U);

    // The fixture reports two VFOs by default, the case where the mode
    // used to be read during transmit.
    std::vector<Transceiver::ModeStatus> statuses;
    QObject::connect (fixture.transceiver.get (), &Transceiver::mode_status,
                      [&] (Transceiver::ModeStatus const& status, unsigned)
                      {
                        statuses.push_back (status);
                      });

    auto requested = fixture.transceiver->state ();
    requested.ptt (true);
    fixture.transceiver->set (requested, 3);
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QVERIFY (fixture.transceiver->state ().ptt ());

    // Some HRD setups report USB for DATA-U while transmitting.
    capabilities.values["Mode"] = "USB";
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::DIG_U);

    requested.ptt (false);
    fixture.transceiver->set (requested, 4);
    capabilities.values["Mode"] = "DATA-U";
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.poll ());
    QVERIFY (!fixture.transceiver->state ().ptt ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::DIG_U);
    for (auto const& status : statuses)
      {
        QVERIFY2 (Transceiver::ModeStatusKind::Preparing == status.kind
                  || Transceiver::DIG_U == status.mode,
                  "mode policy was told about a mode other than DIG_U");
      }

    // readback resumes after transmit
    capabilities.values["Mode"] = "USB";
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::USB);
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void rediscoversModeOrderAfterRestart ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    ScriptedHRDServer::Capabilities capabilities;
    capabilities.dropdowns = QStringList {"Main Mode", "Sub Mode", "Split"};
    capabilities.selections["Main Mode"] = QStringList {"CW", "USB"};
    capabilities.selections["Sub Mode"] = QStringList {"USB", "CW"};
    capabilities.selections["Split"] = QStringList {"Off", "On"};
    capabilities.values["Main Mode"] = "CW";
    capabilities.values["Sub Mode"] = "USB";
    capabilities.values["Split"] = "Off";
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.start ());
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::CW);
    fixture.transceiver->stop ();
    capabilities.radio = "IC-7300";
    capabilities.id = 2;
    capabilities.selections["Main Mode"] = QStringList {"USB", "CW"};
    capabilities.selections["Sub Mode"] = QStringList {"CW", "USB"};
    capabilities.values["Main Mode"] = "USB";
    fixture.server.set_capabilities (capabilities);
    fixture.transceiver->start (2);
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::USB);
    auto requested = fixture.transceiver->state ();
    requested.mode (Transceiver::CW);
    requested.tx_frequency (7074003);
    requested.split (true);
    fixture.transceiver->set (requested, 3);
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    // The legacy interface applies RX mode and TX frequency on successive calls.
    fixture.transceiver->set (requested, 4);
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    auto const commands = fixture.server.commands ();
    QVERIFY (commands.contains ("[2] set dropdown Main~Mode CW 1"));
    QVERIFY (commands.contains ("[2] set dropdown Sub~Mode CW 0"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void dropsDisappearingControlsAfterRestart ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    ScriptedHRDServer::Capabilities capabilities;
    capabilities.radio = "IC-7300";
    capabilities.buttons = QStringList {"Data"};
    capabilities.dropdowns = QStringList {"Receiver", "Main Mode", "Data"};
    capabilities.selections["Receiver"] = QStringList {"Mute / RX", "RX / Off"};
    capabilities.values["Receiver"] = "RX / Off";
    capabilities.selections["Main Mode"] = QStringList {"CW", "USB"};
    capabilities.values["Main Mode"] = "CW";
    capabilities.selections["Data"] = QStringList {"Off", "On"};
    capabilities.values["Data"] = "On";
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.start ());
    QVERIFY (fixture.poll ());
    fixture.transceiver->stop ();
    capabilities.radio = "FTDX-3000";
    capabilities.buttons = QStringList {"TX"};
    capabilities.dropdowns = QStringList {"Main Mode", "Split"};
    capabilities.selections["Split"] = QStringList {"Off", "On"};
    capabilities.values["Split"] = "Off";
    capabilities.values["Main Mode"] = "USB";
    fixture.server.set_capabilities (capabilities);
    fixture.transceiver->start (2);
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::USB);
    auto const before = fixture.server.commands ().size ();
    auto requested = fixture.transceiver->state ();
    requested.mode (Transceiver::CW);
    requested.tx_frequency (0);
    requested.split (false);
    fixture.transceiver->set (requested, 3);
    QVERIFY (fixture.failure.isEmpty ());
    auto const commands = fixture.server.commands ().mid (before);
    QVERIFY (commands.contains ("[1] set dropdown Main~Mode CW 0"));
    QVERIFY (!commands.contains ("[1] set button-select TX 0"));
    QVERIFY (!commands.contains ("[1] get dropdown-text {Receiver}"));
    QVERIFY (!commands.contains ("[1] get dropdown-text {Data}"));
    fixture.transceiver->stop ();
    capabilities.buttons.clear ();
    capabilities.dropdowns.clear ();
    fixture.server.set_capabilities (capabilities);
    fixture.transceiver->start (4);
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().mode (), Transceiver::UNK);
    QVERIFY (fixture.failure.isEmpty ());
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void replacesReceiverWithSplitAfterRestart ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    ScriptedHRDServer::Capabilities capabilities;
    capabilities.dropdowns = QStringList {"Receiver", "Main Mode"};
    capabilities.selections["Receiver"] = QStringList {"Mute / RX", "RX / Off"};
    capabilities.values["Receiver"] = "RX / Off";
    capabilities.selections["Main Mode"] = QStringList {"CW", "USB"};
    capabilities.values["Main Mode"] = "CW";
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.start ());
    QVERIFY (fixture.poll ());
    fixture.transceiver->stop ();
    capabilities.dropdowns = QStringList {"Main Mode", "Split"};
    capabilities.selections["Split"] = QStringList {"Off", "On"};
    capabilities.values["Split"] = "Off";
    fixture.server.set_capabilities (capabilities);
    fixture.transceiver->start (2);
    QVERIFY (fixture.poll ());
    auto requested = fixture.transceiver->state ();
    requested.tx_frequency (7074003);
    requested.split (true);
    fixture.transceiver->set (requested, 3);
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().frequency (), quint64 (10368100000));
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QVERIFY (fixture.server.commands ().contains ("[1] set frequencies-hz 10368100000 7074003"));
    QVERIFY2 (fixture.server.error ().isEmpty (), qPrintable (fixture.server.error ()));
  }

  void clearsReversedVfoAfterRestart ()
  {
    HRDFixture fixture {ScriptedHRDServer::Profile::normal};
    ScriptedHRDServer::Capabilities capabilities;
    capabilities.buttons = QStringList {"VFO A", "VFO B", "TX A", "TX B"};
    capabilities.checked_buttons = QStringList {"VFO B", "TX A"};
    fixture.server.set_capabilities (capabilities);
    QVERIFY (fixture.start ());
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().frequency (), quint64 (7074001));
    fixture.transceiver->stop ();
    capabilities.buttons.clear ();
    capabilities.checked_buttons.clear ();
    fixture.server.set_capabilities (capabilities);
    fixture.transceiver->start (2);
    QVERIFY (fixture.poll ());
    QCOMPARE (fixture.transceiver->state ().frequency (), quint64 (10368100000));
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
