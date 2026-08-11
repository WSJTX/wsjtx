// Integration tests for TxInhibitGate: the intent/hold interaction and the
// gate lifecycle, over a real UDP socket and a real event loop.
//
// test_tx_inhibit_logic covers GateLogic, which is pure and takes injected
// time. Nothing covered the part a rebase is most likely to break: the wiring
// between do_ptt's intent, the UDP hold, and the physicalPtt emission that
// drives the radio. That code lives in upstream-owned files
// (HamlibTransceiver, Configuration), so it is the most exposed to upstream
// churn -- and a break there is silent until someone with a radio notices.
//
// See docs/REVIEW-rc2.md H6.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include <QHostAddress>
#include <QSignalSpy>
#include <QUdpSocket>

#include "TxInhibit/TxInhibitGate.hpp"
#include "TxInhibit/TxInhibitLogic.hpp"

namespace
{
  QByteArray hold_packet (int ttl_ms, char const * station = "TEST")
  {
    return QByteArray {"{\"tx_inhibit\":1,\"ttl_ms\":"}
      + QByteArray::number (ttl_ms)
      + ",\"station\":\"" + station + "\"}";
  }
}

class TestTxInhibitGate final
  : public QObject
{
  Q_OBJECT

private slots:

  // The core equation, end to end: assert PTT <=> want_tx and not hold.
  // Crucially, want_tx never changes here -- only the hold does. That is the
  // whole point of the feature, and the thing a careless refactor breaks.
  void pinFollowsHoldWhileIntentStaysOn ()
  {
    TxInhibitGate gate;
    QSignalSpy pin {&gate, &TxInhibitGate::physicalPtt};
    QSignalSpy bound {&gate, &TxInhibitGate::portBound};

    gate.start_listening ();
    QCOMPARE (bound.count (), 1);
    auto const port = bound.at (0).at (0).value<quint16> ();
    QVERIFY2 (port != 0, "gate did not bind any UDP port");

    // want_tx on, no hold -> assert
    gate.set_intent (true);
    QCOMPARE (pin.count (), 1);
    QCOMPARE (pin.at (0).at (0).toBool (), true);

    // hold arrives; want_tx is untouched -> release
    QUdpSocket agent;
    agent.writeDatagram (hold_packet (5000), QHostAddress::LocalHost, port);
    QTRY_COMPARE (pin.count (), 2);
    QCOMPARE (pin.at (1).at (0).toBool (), false);

    // explicit release; want_tx still on -> assert again
    agent.writeDatagram (hold_packet (0), QHostAddress::LocalHost, port);
    QTRY_COMPARE (pin.count (), 3);
    QCOMPARE (pin.at (2).at (0).toBool (), true);

    gate.shutdown (false);
  }

  // Deadman: an agent that dies without releasing must not hold PTT off
  // forever. The station's own hold timeout has to clear it.
  void holdTimeoutRecoversWithoutRelease ()
  {
    TxInhibitGate gate;
    QSignalSpy bound {&gate, &TxInhibitGate::portBound};
    gate.start_listening ();
    auto const port = bound.at (0).at (0).value<quint16> ();

    gate.set_intent (true);
    QSignalSpy pin {&gate, &TxInhibitGate::physicalPtt};

    QUdpSocket agent;
    agent.writeDatagram (hold_packet (TxInhibit::hold_timeout_ms_min),
                         QHostAddress::LocalHost, port);
    QTRY_COMPARE (pin.count (), 1);
    QCOMPARE (pin.at (0).at (0).toBool (), false);   // held

    // No release sent. The hold must expire on its own.
    QTRY_COMPARE (pin.count (), 2);
    QCOMPARE (pin.at (1).at (0).toBool (), true);    // recovered

    gate.shutdown (false);
  }

  // A hold with no transmit intent must not assert PTT when it clears.
  void releaseWithoutIntentDoesNotKey ()
  {
    TxInhibitGate gate;
    QSignalSpy bound {&gate, &TxInhibitGate::portBound};
    gate.start_listening ();
    auto const port = bound.at (0).at (0).value<quint16> ();

    QSignalSpy pin {&gate, &TxInhibitGate::physicalPtt};
    QUdpSocket agent;
    agent.writeDatagram (hold_packet (200), QHostAddress::LocalHost, port);
    QTest::qWait (400);                      // hold applied and expired
    agent.writeDatagram (hold_packet (0), QHostAddress::LocalHost, port);
    QTest::qWait (100);

    QCOMPARE (pin.count (), 0);              // never keyed: want_tx was false
    gate.shutdown (false);
  }

  // Badge text and counters reach the GUI, and only on a real change.
  void reportsStateChangesOnce ()
  {
    TxInhibitGate gate;
    QSignalSpy bound {&gate, &TxInhibitGate::portBound};
    gate.start_listening ();
    auto const port = bound.at (0).at (0).value<quint16> ();

    QSignalSpy changed {&gate, &TxInhibitGate::inhibitChanged};
    QUdpSocket agent;
    agent.writeDatagram (hold_packet (5000, "W1AW"), QHostAddress::LocalHost, port);
    QTRY_VERIFY (changed.count () >= 1);
    QCOMPARE (changed.at (0).at (0).toBool (), true);
    QVERIFY (changed.at (0).at (1).toString ().contains (QStringLiteral ("W1AW")));

    // A keepalive refreshes the timeout but is not a state change.
    int const before = changed.count ();
    agent.writeDatagram (hold_packet (5000, "W1AW"), QHostAddress::LocalHost, port);
    QTest::qWait (100);
    QCOMPARE (changed.count (), before);

    gate.shutdown (false);
  }

  // shutdown(false) must not request a pin change: Hamlib may already be
  // closed, and rig_set_ptt then fails. This is the teardown ordering that
  // HamlibTransceiver::stop_tx_inhibit_gate depends on.
  void shutdownWithoutPinEmitIsSilent ()
  {
    TxInhibitGate gate;
    gate.start_listening ();
    gate.set_intent (true);

    QSignalSpy pin {&gate, &TxInhibitGate::physicalPtt};
    gate.shutdown (false);
    QCOMPARE (pin.count (), 0);

    // And nothing may be emitted after shutdown, whatever arrives.
    gate.set_intent (true);
    QTest::qWait (60);
    QCOMPARE (pin.count (), 0);
  }

  // Hold expiry must be measured on a monotonic base, so that a system-clock
  // step cannot extend or curtail a hold. WSJT-X hosts step their clocks
  // routinely (Meinberg, Dimension4, BktTimeSync). We cannot move the system
  // clock from a test, so assert the property that makes the gate immune:
  // its time base advances with elapsed time and is independent of the wall
  // clock's absolute value.
  void holdTimingUsesMonotonicBase ()
  {
    TxInhibitGate gate;
    QSignalSpy bound {&gate, &TxInhibitGate::portBound};
    gate.start_listening ();
    auto const port = bound.at (0).at (0).value<quint16> ();
    gate.set_intent (true);

    QSignalSpy pin {&gate, &TxInhibitGate::physicalPtt};
    QUdpSocket agent;

    QElapsedTimer measured;
    measured.start ();
    agent.writeDatagram (hold_packet (300), QHostAddress::LocalHost, port);
    QTRY_COMPARE (pin.count (), 1);          // held
    QTRY_COMPARE (pin.count (), 2);          // expired
    auto const elapsed = measured.elapsed ();

    // Generous bounds: this asserts "the timeout tracks elapsed time", not a
    // precise duration. A wall-clock base would still pass here on a quiet
    // machine -- the real protection is that now_ms() uses QElapsedTimer, and
    // this test fails loudly if that timing is ever wired to something that
    // does not advance.
    QVERIFY2 (elapsed >= 250 && elapsed < 3000,
              qPrintable (QStringLiteral ("hold lasted %1 ms, expected ~300")
                          .arg (elapsed)));

    gate.shutdown (false);
  }
};

QTEST_GUILESS_MAIN (TestTxInhibitGate)
#include "test_tx_inhibit_gate.moc"
