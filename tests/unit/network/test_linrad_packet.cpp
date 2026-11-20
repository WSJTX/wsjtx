#include <QtTest>
#include "Network/LinradPacket.hpp"

class TestLinradPacket : public QObject
{
  Q_OBJECT
private slots:
  void receive_data ()
  {
    QTest::addColumn<int> ("nrx");
    QTest::addColumn<int> ("bytes");
    QTest::addColumn<int> ("frames");
    QTest::newRow ("short-header") << 1 << 23 << 0;
    QTest::newRow ("empty") << 1 << 0 << 0;
    QTest::newRow ("unknown-format") << 0 << 1416 << 0;
    QTest::newRow ("header-only") << 1 << 24 << 348;
    QTest::newRow ("partial-int16") << 1 << 28 << 348;
    QTest::newRow ("partial-float") << -1 << 32 << 174;
    QTest::newRow ("partial-dual-int16") << 2 << 32 << 174;
    QTest::newRow ("partial-dual-float") << -2 << 40 << 87;
    QTest::newRow ("incomplete-int16-frame") << 1 << 27 << 0;
    QTest::newRow ("incomplete-float-frame") << -1 << 28 << 0;
    QTest::newRow ("incomplete-dual-int16-frame") << 2 << 28 << 0;
    QTest::newRow ("incomplete-dual-float-frame") << -2 << 32 << 0;
    QTest::newRow ("full") << 1 << 1416 << 348;
    QTest::newRow ("extension") << 1 << 1423 << 348;
  }


  void receiveVariableLength_data ()
  {
    QTest::addColumn<int> ("nrx");
    QTest::addColumn<int> ("bytes");
    QTest::addColumn<int> ("frames");
    QTest::newRow ("header-only") << 1 << 24 << 0;
    QTest::newRow ("single-int16-frame") << 1 << 28 << 1;
    QTest::newRow ("standard-int16") << 1 << 1416 << 348;
    QTest::newRow ("large-int16") << 1 << 4120 << 1024;
    QTest::newRow ("large-float") << -1 << 4120 << 512;
    QTest::newRow ("large-dual-int16") << 2 << 4120 << 512;
    QTest::newRow ("large-dual-float") << -2 << 4120 << 256;
    QTest::newRow ("partial-int16-frame") << 1 << 4119 << 0;
    QTest::newRow ("partial-float-frame") << -1 << 4116 << 0;
    QTest::newRow ("extension") << 1 << 4127 << 1024;
  }

  void receiveVariableLength ()
  {
    QFETCH (int, nrx);
    QFETCH (int, bytes);
    QFETCH (int, frames);
    QUdpSocket receiver, sender;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));
    QByteArray input (bytes, '\x35');
    input[22] = static_cast<char> (nrx);
    LinradPacketBuffer<512> packet;
    QCOMPARE (sender.writeDatagram (input, QHostAddress::LocalHost,
                                   receiver.localPort ()), qint64 (bytes));
    QVERIFY (receiver.waitForReadyRead (1000));
    QCOMPARE (packet.receive (receiver, false), frames);
    QVERIFY (!receiver.hasPendingDatagrams ());
    if (frames)
      {
        auto expected = input.left (qMin (bytes, 4120));
        expected.append (QByteArray (4120 - expected.size (), '\0'));
        QCOMPARE (QByteArray (reinterpret_cast<char const *> (&packet), sizeof packet), expected);
      }
  }

  void receive ()
  {
    QFETCH (int, nrx);
    QFETCH (int, bytes);
    QFETCH (int, frames);
    QUdpSocket receiver, sender;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));
    QByteArray input (qMax (bytes, 1416), '\x35');
    input[22] = static_cast<char> (nrx);
    LinradPacket packet;
    // Reuse a populated packet to catch retention of a previous audio tail.
    input[22] = 1;
    QCOMPARE (sender.writeDatagram (input.left (1416), QHostAddress::LocalHost,
                                   receiver.localPort ()), qint64 (1416));
    QVERIFY (receiver.waitForReadyRead (1000));
    QCOMPARE (packet.receive (receiver), 348);
    input[22] = static_cast<char> (nrx);
    QCOMPARE (sender.writeDatagram (input.left (bytes), QHostAddress::LocalHost,
                                   receiver.localPort ()), qint64 (bytes));
    QVERIFY (receiver.waitForReadyRead (1000));
    QCOMPARE (packet.receive (receiver), frames);
    QVERIFY (!receiver.hasPendingDatagrams ());
    if (frames)
      {
        auto expected = input.left (qMin (bytes, 1416));
        expected.append (QByteArray (1416 - expected.size (), '\0'));
        QCOMPARE (QByteArray (reinterpret_cast<char const *> (&packet), sizeof packet), expected);
      }
  }
};

QTEST_GUILESS_MAIN (TestLinradPacket)
#include "test_linrad_packet.moc"
