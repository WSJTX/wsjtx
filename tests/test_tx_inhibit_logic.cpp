#include <QtTest>

#include "TxInhibit/TxInhibitLogic.hpp"

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

    // Hold for 500 ms
    QByteArray hold = R"({"tx_inhibit":1,"ttl_ms":500,"station":"TEST","seq":1})";
    QVERIFY (g.on_datagram (hold, t));
    QVERIFY (g.line_inhibited (t));
    QVERIFY (!g.radiate (true, t));
    QVERIFY (!g.radiate (false, t));

    // After hold timeout: free again
    QVERIFY (!g.line_inhibited (t + 501));
    QVERIFY (g.radiate (true, t + 501));
    QCOMPARE (g.expiries (), 1u);
  }

  void releaseClearsHold ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 2000000;
    QByteArray hold = R"({"tx_inhibit":1,"ttl_ms":1000,"station":"KEY","seq":2})";
    QVERIFY (g.on_datagram (hold, t));
    QVERIFY (!g.radiate (true, t));

    QByteArray rel = R"({"tx_inhibit":1,"ttl_ms":0,"station":"KEY","seq":3})";
    QVERIFY (g.on_datagram (rel, t + 10));
    QVERIFY (g.radiate (true, t + 10));
    QCOMPARE (g.hold_rx (), 1u);
    QCOMPARE (g.release_rx (), 1u);
  }

  void invalidProtocolVersionIgnored ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 3000000;
    QByteArray bad = R"({"tx_inhibit":2,"ttl_ms":500,"station":"X"})";
    QVERIFY (!g.on_datagram (bad, t));
    QVERIFY (g.radiate (true, t));
    QCOMPARE (g.invalid (), 1u);
  }

  void missingTtlIgnored ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 4000000;
    QByteArray bad = R"({"tx_inhibit":1,"station":"X"})";
    QVERIFY (!g.on_datagram (bad, t));
    QVERIFY (g.radiate (true, t));
    QCOMPARE (g.invalid (), 1u);
  }

  void outOfRangeTtlIgnored ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 5000000;
    // Below min (100)
    QVERIFY (!g.on_datagram (R"({"tx_inhibit":1,"ttl_ms":50})", t));
    // Above max (30000)
    QVERIFY (!g.on_datagram (R"({"tx_inhibit":1,"ttl_ms":30001})", t));
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
    QByteArray hold = R"({"tx_inhibit":1,"ttl_ms":500,"station":"A","seq":1})";
    QVERIFY (g.on_datagram (hold, t));          // free → held
    QByteArray keep = R"({"tx_inhibit":1,"ttl_ms":500,"station":"A","seq":2})";
    QVERIFY (!g.on_datagram (keep, t + 100));   // still held
    QVERIFY (g.line_inhibited (t + 100));
    QCOMPARE (g.hold_rx (), 2u);
  }

  void redundantReleaseNoFlip ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 8000000;
    QByteArray rel = R"({"tx_inhibit":1,"ttl_ms":0})";
    QVERIFY (!g.on_datagram (rel, t)); // already free
    QVERIFY (g.radiate (true, t));
    QCOMPARE (g.release_rx (), 1u);
  }

  void badgeTextWithAndWithoutStation ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 9000000;
    QCOMPARE (g.badge_text (t), QString {});

    QVERIFY (g.on_datagram (R"({"tx_inhibit":1,"ttl_ms":500})", t));
    QCOMPARE (g.badge_text (t), QStringLiteral ("TX INHIBITED"));

    // Keepalive / refresh may not flip hold level (returns false) but updates station.
    (void) g.on_datagram (R"({"tx_inhibit":1,"ttl_ms":500,"station":"W1AW"})", t + 1);
    QCOMPARE (g.badge_text (t + 1), QStringLiteral ("TX INHIBITED — held by W1AW"));
  }

  void stringTtlAccepted ()
  {
    TxInhibit::GateLogic g;
    qint64 t = 10000000;
    // Interop: ttl_ms as string digits
    QVERIFY (g.on_datagram (R"({"tx_inhibit":1,"ttl_ms":"400","station":"S"})", t));
    QVERIFY (g.line_inhibited (t));
  }
};

QTEST_MAIN (TestTxInhibitLogic)
#include "test_tx_inhibit_logic.moc"
