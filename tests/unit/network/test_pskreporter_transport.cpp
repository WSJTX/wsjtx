#include <QtTest>

#include <QHostAddress>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include "Network/PSKReporter.hpp"

namespace
{
  quint16 u16(QByteArray const& bytes, int offset)
  {
    return (static_cast<quint8>(bytes[offset]) << 8)
      | static_cast<quint8>(bytes[offset + 1]);
  }

  QList<quint16> setIds(QByteArray const& payload)
  {
    QList<quint16> ids;
    int offset = 16;
    while (offset + 4 <= payload.size()) {
      auto const length = u16(payload, offset + 2);
      ids.append(u16(payload, offset));
      if (length < 4) {
        break;
      }
      offset += length;
    }
    return ids;
  }

  void verifyReport(QByteArray const& payload, bool includeDescriptors)
  {
    QVERIFY(payload.size() >= 16);
    QCOMPARE(u16(payload, 0), quint16(10));
    QCOMPARE(u16(payload, 2), static_cast<quint16>(payload.size()));

    auto const ids = setIds(payload);
    QVERIFY(ids.contains(quint16(0x50e2)));
    QVERIFY(ids.contains(quint16(0x50e3)));
    QCOMPARE(ids.contains(quint16(2)), includeDescriptors);
    QCOMPARE(ids.contains(quint16(3)), includeDescriptors);
  }

  PSKReporter::Options options(bool useTcpIp, quint16 port)
  {
    return {useTcpIp, QString {}, "MAP65 transport test", "127.0.0.1", port};
  }

  bool addSpots(PSKReporter& reporter, QString const& prefix,
                QStringList& callsigns)
  {
    for (int i = 0; i < 2048; ++i) {
      auto suffixIndex = i;
      QString suffix;
      for (int j = 0; j < 4; ++j) {
        suffix.prepend(QChar {
          static_cast<ushort>('A' + (suffixIndex % 26))});
        suffixIndex /= 26;
      }
      auto const callsign = prefix + QString::number((i / 26) % 10) + suffix;
      if (!reporter.addRemoteStation(
            callsign, "FN20", 50313000u, "Q65-60A", -12,
            QDateTime {{2026, 8, 17}, {12, 0, 0}, Qt::UTC})) {
        return false;
      }
      callsigns.append(callsign);
    }
    return true;
  }

  void accountForPayload(QByteArray const& payload, QSet<QString>& remaining)
  {
    for (auto const& callsign : remaining.values()) {
      if (payload.contains(callsign.toUtf8())) {
        remaining.remove(callsign);
      }
    }
  }

  QList<QByteArray> takeCompleteFrames(QByteArray& stream)
  {
    QList<QByteArray> frames;
    while (stream.size() >= 4) {
      auto const length = u16(stream, 2);
      if (length < 16 || stream.size() < length) {
        break;
      }
      frames.append(stream.left(length));
      stream.remove(0, length);
    }
    return frames;
  }
}

class TestPSKReporterTransport final : public QObject
{
  Q_OBJECT

private slots:
  void sendsAllIpfixReportsOverUdp()
  {
    QUdpSocket receiver;
    QVERIFY(receiver.bind(QHostAddress {QHostAddress::LocalHost}, 0));

    PSKReporter reporter {options(false, receiver.localPort())};
    reporter.setLocalStation("K1ABC", "FN21", "N/A", "N/A (MAP65)");
    QStringList callsigns;
    QVERIFY(addSpots(reporter, "UA", callsigns));
    QSet<QString> remaining {callsigns.begin(), callsigns.end()};

    reporter.sendReport();
    QList<QByteArray> reports;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      while (receiver.hasPendingDatagrams()) {
        QByteArray payload;
        payload.resize(static_cast<int>(receiver.pendingDatagramSize()));
        if (receiver.readDatagram(payload.data(), payload.size())
            != static_cast<qint64>(payload.size())) {
          return false;
        }
        reports.append(payload);
        accountForPayload(payload, remaining);
      }
      return reports.size() > 1 && remaining.isEmpty();
    }()), 5000);

    QVERIFY(reports.size() > 1);
    for (int i = 0; i < reports.size(); ++i) {
      verifyReport(reports.at(i), i == 0);
    }
    reporter.sendReport(true);
  }

  void sendsAllIpfixReportsOverTcp()
  {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress {QHostAddress::LocalHost}, 0));

    PSKReporter reporter {options(true, server.serverPort())};
    reporter.setLocalStation("K1ABC", "FN21", "N/A", "N/A (MAP65)");
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
    auto *peer = server.nextPendingConnection();
    QVERIFY(peer);

    QStringList callsigns;
    QVERIFY(addSpots(reporter, "TA", callsigns));
    QSet<QString> remaining {callsigns.begin(), callsigns.end()};
    QByteArray stream;
    QList<QByteArray> reports;
    reporter.sendReport();

    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      stream.append(peer->readAll());
      auto const newFrames = takeCompleteFrames(stream);
      for (auto const& frame : newFrames) {
        reports.append(frame);
        accountForPayload(frame, remaining);
      }
      return reports.size() > 1 && remaining.isEmpty();
    }()), 5000);

    QVERIFY(reports.size() > 1);
    for (int i = 0; i < reports.size(); ++i) {
      verifyReport(reports.at(i), i == 0);
    }
    reporter.sendReport(true);
  }
};

QTEST_MAIN(TestPSKReporterTransport)

#include "test_pskreporter_transport.moc"
