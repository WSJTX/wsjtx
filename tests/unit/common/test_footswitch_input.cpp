// Unit tests for FootswitchInput: debounce, arming after release, pin
// selection and the guarantee that stop() or a port error always
// releases.

#include <QList>
#include <QSignalSpy>
#include <QtTest>

#include "FootswitchInput.hpp"

namespace
{
  using Pin = FootswitchInput::Pin;
  using Reading = FootswitchInput::Reading;
  int constexpr N = FootswitchInput::debounce_count;

  // Feeds FootswitchInput a scripted sequence of line states.
  struct Script
  {
    QList<Reading> readings;
    int next {0};

    FootswitchInput::Reader reader ()
    {
      return [this] {
        return next < readings.size () ? readings.at (next++) : readings.last ();
      };
    }
  };

  Reading lines (QSerialPort::PinoutSignals signal_lines)
  {
    Reading r;
    r.lines = signal_lines;
    return r;
  }

  Reading high () {return lines (QSerialPort::ClearToSendSignal);}
  Reading low () {return lines (QSerialPort::NoSignal);}

  Reading failure (QString const& reason)
  {
    Reading r;
    r.ok = false;
    r.error = reason;
    return r;
  }

  QList<Reading> repeat (Reading r, int n)
  {
    QList<Reading> list;
    for (int i = 0; i < n; ++i) list << r;
    return list;
  }

  // Released long enough to arm.
  QList<Reading> armed () {return repeat (low (), N);}

  void pollTimes (FootswitchInput& input, int n)
  {
    for (int i = 0; i < n; ++i) input.poll ();
  }
}

class TestFootswitchInput final
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void armsAfterStableRelease ()
  {
    Script script {armed ()};
    FootswitchInput input;
    QSignalSpy ready {&input, &FootswitchInput::readyAfterRelease};
    input.start (script.reader (), Pin::CTS);
    pollTimes (input, N - 1);
    QVERIFY (!input.isArmed ());
    input.poll ();
    QVERIFY (input.isArmed ());
    QCOMPARE (ready.count (), 0); // only reported after a held switch
  }

  Q_SLOT void pressReportedAfterDebounce ()
  {
    Script script {armed () + repeat (high (), N + 1)};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    input.start (script.reader (), Pin::CTS);
    pollTimes (input, N);

    pollTimes (input, N - 1);
    QCOMPARE (pressed.count (), 0);
    QVERIFY (!input.isPressed ());

    input.poll ();
    QCOMPARE (pressed.count (), 1);
    QVERIFY (input.isPressed ());

    input.poll ();              // holding the switch reports nothing more
    QCOMPARE (pressed.count (), 1);
  }

  Q_SLOT void heldAtStartIsIgnoredUntilReleased ()
  {
    Script script {repeat (high (), 10) + armed () + repeat (high (), N)};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    QSignalSpy waiting {&input, &FootswitchInput::waitingForRelease};
    QSignalSpy ready {&input, &FootswitchInput::readyAfterRelease};
    input.start (script.reader (), Pin::CTS);

    pollTimes (input, 10);      // closed from the start: never a press
    QCOMPARE (pressed.count (), 0);
    QCOMPARE (waiting.count (), 1);
    QCOMPARE (ready.count (), 0);
    QVERIFY (!input.isArmed ());

    pollTimes (input, N);       // released: now armed
    QVERIFY (input.isArmed ());
    QCOMPARE (ready.count (), 1);
    pollTimes (input, N);       // a fresh press counts
    QCOMPARE (pressed.count (), 1);
    QCOMPARE (waiting.count (), 1);
  }

  Q_SLOT void restartRequiresReleaseAgain ()
  {
    Script first {armed () + repeat (high (), N)};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (first.reader (), Pin::CTS);
    pollTimes (input, 2 * N);
    QCOMPARE (pressed.count (), 1);

    // Reconfigured while the switch is still held: released, and the
    // held switch must not key again by itself.
    Script second {repeat (high (), 10)};
    input.start (second.reader (), Pin::CTS);
    QCOMPARE (released.count (), 1);
    pollTimes (input, 10);
    QCOMPARE (pressed.count (), 1);
  }

  Q_SLOT void singleGlitchIgnored ()
  {
    Script script {armed () + QList<Reading> {high (), low (), low (), low ()}};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS);

    pollTimes (input, script.readings.size ());
    QCOMPARE (pressed.count (), 0);
    QCOMPARE (released.count (), 0);
  }

  Q_SLOT void contactBounceGivesOnePressAndOneRelease ()
  {
    Script script {armed () + QList<Reading> {
        high (), low (), high (), low (), high (), high (), high (), high (),
        low (), high (), low (), low (), low (), low ()}};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS);

    pollTimes (input, script.readings.size ());
    QCOMPARE (pressed.count (), 1);
    QCOMPARE (released.count (), 1);
    QVERIFY (!input.isPressed ());
  }

  Q_SLOT void inactiveLineIsReleased ()
  {
    // Unplugged cable, broken wire or unpowered adapter: the line is low.
    Script script {repeat (low (), 20)};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    input.start (script.reader (), Pin::CTS);
    pollTimes (input, 20);
    QCOMPARE (pressed.count (), 0);
    QVERIFY (!input.isPressed ());
  }

  Q_SLOT void stopWhilePressedReleases ()
  {
    Script script {armed () + repeat (high (), N)};
    FootswitchInput input;
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS);
    pollTimes (input, 2 * N);
    QVERIFY (input.isPressed ());

    input.stop ();
    QCOMPARE (released.count (), 1);
    QVERIFY (!input.isPressed ());
    QVERIFY (!input.isActive ());

    input.stop ();              // a second stop reports nothing more
    QCOMPARE (released.count (), 1);
  }

  Q_SLOT void stopWhileReleasedIsSilent ()
  {
    Script script {armed ()};
    FootswitchInput input;
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS);
    pollTimes (input, N);

    input.stop ();
    QCOMPARE (released.count (), 0);
  }

  Q_SLOT void portErrorWhilePressedReleasesAndReports ()
  {
    Script script {armed () + repeat (high (), N) + QList<Reading> {failure ("device removed")}};
    FootswitchInput input;
    QSignalSpy released {&input, &FootswitchInput::released};
    QSignalSpy error {&input, &FootswitchInput::error};
    input.start (script.reader (), Pin::CTS);
    pollTimes (input, 2 * N);
    QVERIFY (input.isPressed ());

    input.poll ();
    QCOMPARE (released.count (), 1);
    QCOMPARE (error.count (), 1);
    QVERIFY (error.at (0).at (0).toString ().contains ("device removed"));
    QVERIFY (!input.isPressed ());
    QVERIFY (!input.isActive ());
  }

  Q_SLOT void readerErrorDoesNotRetry ()
  {
    Script script {{failure ("gone")}};
    FootswitchInput input;
    input.start (script.reader (), Pin::CTS);
    input.poll ();
    QVERIFY (!input.isActive ());
    QVERIFY (!input.isRetrying ());   // only a real serial port is retried
  }

  Q_SLOT void missingPortReportsErrorAndKeepsRetrying ()
  {
    FootswitchInput input;
    QSignalSpy error {&input, &FootswitchInput::error};
    QVERIFY (!input.start (QStringLiteral ("WSJTX_TEST_NO_SUCH_PORT"), Pin::CTS));
    QCOMPARE (error.count (), 1);
    QVERIFY (!input.isActive ());
    QVERIFY (input.isRetrying ());

    input.stop ();                    // stop() cancels the retries
    QVERIFY (!input.isRetrying ());
  }

  Q_SLOT void portGuardIsAskedBeforeEveryOpen ()
  {
    FootswitchInput input;
    QStringList asked;
    input.setPortGuard ([&asked] (QString const& port) {
        asked << port;
        return QStringLiteral ("also the PTT port");
      });
    QSignalSpy error {&input, &FootswitchInput::error};
    QVERIFY (!input.start (QStringLiteral ("WSJTX_TEST_NO_SUCH_PORT"), Pin::CTS));
    QCOMPARE (asked, QStringList {QStringLiteral ("WSJTX_TEST_NO_SUCH_PORT")});
    QCOMPARE (error.count (), 1);
    QVERIFY (error.at (0).at (0).toString ().contains (QStringLiteral ("also the PTT port")));
    QVERIFY (input.isRetrying ());

    // Each reconnection attempt asks again.
    QTRY_COMPARE_WITH_TIMEOUT (asked.size (), 2, FootswitchInput::retry_interval_ms + 2000);
    input.stop ();
    QVERIFY (!input.isRetrying ());
  }

  Q_SLOT void eachPinReadsItsOwnLine_data ()
  {
    QTest::addColumn<int> ("pin");
    QTest::addColumn<int> ("line");
    QTest::newRow ("CTS") << int (Pin::CTS) << int (QSerialPort::ClearToSendSignal);
    QTest::newRow ("DSR") << int (Pin::DSR) << int (QSerialPort::DataSetReadySignal);
    QTest::newRow ("DCD") << int (Pin::DCD) << int (QSerialPort::DataCarrierDetectSignal);
    QTest::newRow ("RI")  << int (Pin::RI)  << int (QSerialPort::RingIndicatorSignal);
  }

  Q_SLOT void eachPinReadsItsOwnLine ()
  {
    QFETCH (int, pin);
    QFETCH (int, line);
    auto const own = QSerialPort::PinoutSignals (line);
    auto const others = QSerialPort::PinoutSignals (
      (QSerialPort::ClearToSendSignal | QSerialPort::DataSetReadySignal
       | QSerialPort::DataCarrierDetectSignal | QSerialPort::RingIndicatorSignal) & ~line);

    // Every other line active: counts as released, and arms.
    {
      Script script {repeat (lines (others), N + 1)};
      FootswitchInput input;
      QSignalSpy pressed {&input, &FootswitchInput::pressed};
      input.start (script.reader (), static_cast<Pin> (pin));
      pollTimes (input, N + 1);
      QCOMPARE (pressed.count (), 0);
      QVERIFY (input.isArmed ());
    }

    // Released, then only the chosen line active: pressed.
    {
      Script script {armed () + repeat (lines (own), N)};
      FootswitchInput input;
      QSignalSpy pressed {&input, &FootswitchInput::pressed};
      input.start (script.reader (), static_cast<Pin> (pin));
      pollTimes (input, 2 * N);
      QCOMPARE (pressed.count (), 1);
    }
  }
};

QTEST_GUILESS_MAIN (TestFootswitchInput)

#include "test_footswitch_input.moc"
