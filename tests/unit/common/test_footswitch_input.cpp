// Unit tests for FootswitchInput: debounce, invert, pin selection and
// the guarantee that stop() or a port error always releases.

#include <QList>
#include <QSignalSpy>
#include <QtTest>

#include "FootswitchInput.hpp"

namespace
{
  using Pin = FootswitchInput::Pin;
  using Reading = FootswitchInput::Reading;

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
  Q_SLOT void pressReportedAfterDebounce ()
  {
    Script script {{high (), high (), high (), high ()}};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    input.start (script.reader (), Pin::CTS, false);

    pollTimes (input, FootswitchInput::debounce_count - 1);
    QCOMPARE (pressed.count (), 0);
    QVERIFY (!input.isPressed ());

    input.poll ();
    QCOMPARE (pressed.count (), 1);
    QVERIFY (input.isPressed ());

    input.poll ();              // holding the switch reports nothing more
    QCOMPARE (pressed.count (), 1);
  }

  Q_SLOT void singleGlitchIgnored ()
  {
    Script script {{low (), high (), low (), low (), low ()}};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS, false);

    pollTimes (input, script.readings.size ());
    QCOMPARE (pressed.count (), 0);
    QCOMPARE (released.count (), 0);
  }

  Q_SLOT void contactBounceGivesOnePressAndOneRelease ()
  {
    Script script {{high (), low (), high (), low (), high (), high (), high (), high (),
                    low (), high (), low (), low (), low (), low ()}};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS, false);

    pollTimes (input, script.readings.size ());
    QCOMPARE (pressed.count (), 1);
    QCOMPARE (released.count (), 1);
    QVERIFY (!input.isPressed ());
  }

  Q_SLOT void invertSwapsPressedAndReleased ()
  {
    Script script {{low (), low (), low (), high (), high (), high ()}};
    FootswitchInput input;
    QSignalSpy pressed {&input, &FootswitchInput::pressed};
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS, true);

    pollTimes (input, 3);       // line low = pressed when inverted
    QCOMPARE (pressed.count (), 1);
    QVERIFY (input.isPressed ());

    pollTimes (input, 3);       // line high = released when inverted
    QCOMPARE (released.count (), 1);
    QVERIFY (!input.isPressed ());
  }

  Q_SLOT void stopWhilePressedReleases ()
  {
    Script script {{high ()}};
    FootswitchInput input;
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS, false);
    pollTimes (input, FootswitchInput::debounce_count);
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
    Script script {{low ()}};
    FootswitchInput input;
    QSignalSpy released {&input, &FootswitchInput::released};
    input.start (script.reader (), Pin::CTS, false);
    pollTimes (input, FootswitchInput::debounce_count);

    input.stop ();
    QCOMPARE (released.count (), 0);
  }

  Q_SLOT void portErrorWhilePressedReleasesAndReports ()
  {
    Script script {{high (), high (), high (), failure ("device removed")}};
    FootswitchInput input;
    QSignalSpy released {&input, &FootswitchInput::released};
    QSignalSpy error {&input, &FootswitchInput::error};
    input.start (script.reader (), Pin::CTS, false);
    pollTimes (input, FootswitchInput::debounce_count);
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
    input.start (script.reader (), Pin::CTS, false);
    input.poll ();
    QVERIFY (!input.isActive ());
    QVERIFY (!input.isRetrying ());   // only a real serial port is retried
  }

  Q_SLOT void missingPortReportsErrorAndKeepsRetrying ()
  {
    FootswitchInput input;
    QSignalSpy error {&input, &FootswitchInput::error};
    QVERIFY (!input.start (QStringLiteral ("WSJTX_TEST_NO_SUCH_PORT"), Pin::CTS, false));
    QCOMPARE (error.count (), 1);
    QVERIFY (!input.isActive ());
    QVERIFY (input.isRetrying ());

    input.stop ();                    // stop() cancels the retries
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

    // Every other line active: must not count as pressed.
    {
      Script script {{lines (others)}};
      FootswitchInput input;
      QSignalSpy pressed {&input, &FootswitchInput::pressed};
      input.start (script.reader (), static_cast<Pin> (pin), false);
      pollTimes (input, FootswitchInput::debounce_count + 1);
      QCOMPARE (pressed.count (), 0);
    }

    // Only the chosen line active: pressed.
    {
      Script script {{lines (own)}};
      FootswitchInput input;
      QSignalSpy pressed {&input, &FootswitchInput::pressed};
      input.start (script.reader (), static_cast<Pin> (pin), false);
      pollTimes (input, FootswitchInput::debounce_count);
      QCOMPARE (pressed.count (), 1);
    }
  }
};

QTEST_GUILESS_MAIN (TestFootswitchInput)

#include "test_footswitch_input.moc"
