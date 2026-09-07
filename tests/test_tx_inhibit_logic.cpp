#include <QtTest>

#include "TxInhibit/TxInhibitLogic.hpp"

namespace
{
  QByteArray hold (char const * controller, int ttl_ms, char const * station = "")
  {
    return TxInhibit::build_datagram (QString::fromUtf8 (controller)
                                      , static_cast<quint32> (ttl_ms)
                                      , QString::fromUtf8 (station));
  }
}

class TestTxInhibitLogic : public QObject
{
  Q_OBJECT

private slots:
  void radiateIsIntentAndNotHold ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 1000000;
    QVERIFY (g.radiate (true, t));
    QVERIFY (!g.radiate (false, t));

    QVERIFY (g.on_datagram (hold ("A", 500, "TEST"), t));
    QVERIFY (g.line_inhibited (t));
    QVERIFY (!g.radiate (true, t));
    QVERIFY (!g.radiate (false, t));

    QVERIFY (!g.line_inhibited (t + 501));
    QVERIFY (g.radiate (true, t + 501));
    QCOMPARE (g.expiries (), 1u);
  }

  void releaseClearsOwnHold ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 2000000;
    QVERIFY (g.on_datagram (hold ("KEY", 1000, "KEY"), t));
    QVERIFY (!g.radiate (true, t));

    QVERIFY (g.on_datagram (hold ("KEY", 0, "KEY"), t + 10));
    QVERIFY (g.radiate (true, t + 10));
    QCOMPARE (g.hold_rx (), 1u);
    QCOMPARE (g.release_rx (), 1u);
  }

  void otherControllerCannotRelease ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 2500000;
    QVERIFY (g.on_datagram (hold ("A", 1000, "SSB"), t));
    QVERIFY (!g.on_datagram (hold ("B", 0, "CW"), t + 1)); // still held by A
    QVERIFY (g.line_inhibited (t + 1));
    QCOMPARE (g.lease_count (t + 1), 1);
    QCOMPARE (g.release_rx (), 1u);
  }

  void multiControllerOr ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 2600000;
    QVERIFY (g.on_datagram (hold ("A", 1000, "SSB"), t));
    QVERIFY (!g.on_datagram (hold ("B", 1000, "CW"), t + 1)); // still held
    QCOMPARE (g.lease_count (t + 1), 2);
    QVERIFY (!g.on_datagram (hold ("A", 0), t + 2)); // B still holding
    QVERIFY (g.line_inhibited (t + 2));
    QCOMPARE (g.lease_count (t + 2), 1);
    QVERIFY (g.on_datagram (hold ("B", 0), t + 3));
    QVERIFY (!g.line_inhibited (t + 3));
  }

  void perRowDeadman ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 2700000;
    QVERIFY (g.on_datagram (hold ("A", 500, "A"), t));
    QVERIFY (!g.on_datagram (hold ("B", 2000, "B"), t));
    QVERIFY (g.line_inhibited (t + 501)); // A expired, B live
    QCOMPARE (g.lease_count (t + 501), 1);
    QCOMPARE (g.expiries (), 1u);
    QVERIFY (!g.line_inhibited (t + 2001));
    QCOMPARE (g.expiries (), 2u);
  }

  void emptyControllerRejected ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 3000000;
    QByteArray bad = TxInhibit::build_datagram (QString {}, 500, QStringLiteral ("X"));
    QVERIFY (!g.on_datagram (bad, t));
    QVERIFY (g.radiate (true, t));
    QCOMPARE (g.invalid (), 1u);
  }

  void wrongMessageTypeIgnored ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 3100000;
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::HaltTx, QString {}
                                 , NetworkMessage::Builder::schema_number};
    out << false;
    QVERIFY (!g.on_datagram (message, t));
    QCOMPARE (g.invalid (), 1u);
  }

  void outOfRangeTtlIgnored ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 5000000;
    QVERIFY (!g.on_datagram (hold ("A", 50), t));
    QVERIFY (!g.on_datagram (hold ("A", 30001), t));
    QVERIFY (g.radiate (true, t));
    QCOMPARE (g.invalid (), 2u);
  }

  void oversizedDatagramIgnored ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 6000000;
    QByteArray big (TxInhibit::max_datagram_bytes + 1, 'x');
    QVERIFY (!g.on_datagram (big, t));
    QCOMPARE (g.invalid (), 1u);
  }

  void keepaliveDoesNotFlipLevel ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 7000000;
    QVERIFY (g.on_datagram (hold ("A", 500, "A"), t));
    QVERIFY (!g.on_datagram (hold ("A", 500, "A"), t + 100));
    QVERIFY (g.line_inhibited (t + 100));
    QCOMPARE (g.hold_rx (), 2u);
  }

  void redundantReleaseNoFlip ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 8000000;
    QVERIFY (!g.on_datagram (hold ("A", 0), t));
    QVERIFY (g.radiate (true, t));
    QCOMPARE (g.release_rx (), 1u);
  }

  void badgeTextWithMultipleHolders ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 9000000;
    QCOMPARE (g.badge_text (t), QString {});

    QVERIFY (g.on_datagram (hold ("A", 500), t));
    QCOMPARE (g.badge_text (t), QStringLiteral ("TX INHIBITED"));

    (void) g.on_datagram (hold ("A", 500, "W1AW"), t + 1);
    QCOMPARE (g.badge_text (t + 1), QStringLiteral ("TX INHIBITED — held by W1AW"));

    (void) g.on_datagram (hold ("B", 500, "W2SZ"), t + 2);
    QCOMPARE (g.badge_text (t + 2), QStringLiteral ("TX INHIBITED — held by W1AW, W2SZ"));
  }

  void jsonNoLongerAccepted ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 10000000;
    QByteArray json = R"({"tx_inhibit":1,"ttl_ms":400,"station":"S"})";
    QVERIFY (!g.on_datagram (json, t));
    QVERIFY (g.radiate (true, t));
    QCOMPARE (g.invalid (), 1u);
  }
};

QTEST_MAIN (TestTxInhibitLogic)
#include "test_tx_inhibit_logic.moc"
