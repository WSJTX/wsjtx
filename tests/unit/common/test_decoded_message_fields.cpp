#include <QtTest>

#include "Decoder/decodedtext.h"

class TestDecodedMessageFields final : public QObject
{
  Q_OBJECT

private slots:
  void parsesMessage_data();
  void parsesMessage();
};

void TestDecodedMessageFields::parsesMessage_data()
{
  QTest::addColumn<QString> ("message");
  QTest::addColumn<QString> ("destination");
  QTest::addColumn<QString> ("sender");
  QTest::addColumn<QString> ("grid");
  QTest::addColumn<bool> ("cq");

  QTest::newRow ("cq") << "CQ K1ABC FN42" << "CQ" << "K1ABC" << "FN42" << true;
  QTest::newRow ("cq-dx") << "CQ DX K1ABC FN42" << "CQ DX" << "K1ABC" << "FN42" << true;
  QTest::newRow ("compact-cq-dx") << "CQDX K1ABC FN42" << "CQDX" << "K1ABC" << "FN42" << true;
  QTest::newRow ("dx-prefix") << "CQ DX1ABC JO00" << "CQ" << "DX1ABC" << "JO00" << true;
  QTest::newRow ("letter-modifier") << "CQ POTA W1AW FN31" << "CQ POTA" << "W1AW" << "FN31" << true;
  QTest::newRow ("test-modifier") << "CQ TEST KA1GT FN54" << "CQ TEST" << "KA1GT" << "FN54" << true;
  QTest::newRow ("numeric-modifier") << "CQ 123 K1ABC FN42" << "CQ 123" << "K1ABC" << "FN42" << true;
  QTest::newRow ("directed-grid") << "K1ABC W1AW FN31" << "K1ABC" << "W1AW" << "FN31" << false;
  QTest::newRow ("directed-report") << "K1ABC W1AW -10" << "K1ABC" << "W1AW" << "" << false;
  QTest::newRow ("portable-suffix") << "CQ W3SZ/2 FN20" << "CQ" << "W3SZ/2" << "FN20" << true;
  QTest::newRow ("rover-suffix") << "CQ K1ABC/R FN42" << "CQ" << "K1ABC/R" << "FN42" << true;
  QTest::newRow ("portable-marker") << "CQ K1ABC/P FN42" << "CQ" << "K1ABC/P" << "FN42" << true;
  QTest::newRow ("compound-prefix") << "CQ 3DA0RS JJ22" << "CQ" << "3DA0RS" << "JJ22" << true;
  QTest::newRow ("bracketed-call") << "CQ <PJ4/KA1ABC> FK52" << "CQ" << "PJ4/KA1ABC" << "FK52" << true;
  QTest::newRow ("later-grid") << "K1ABC W1AW R 520001 FN31AA" << "K1ABC" << "W1AW" << "FN31AA" << false;
  QTest::newRow ("incomplete") << "CQ TEST" << "CQ" << "TEST" << "" << true;
  QTest::newRow ("free-text") << "HELLO WORLD" << "HELLO" << "WORLD" << "" << false;
}

void TestDecodedMessageFields::parsesMessage()
{
  QFETCH (QString, message);
  QFETCH (QString, destination);
  QFETCH (QString, sender);
  QFETCH (QString, grid);
  QFETCH (bool, cq);

  auto const fields = parseDecodedMessage (message);
  QCOMPARE (fields.destination, destination);
  QCOMPARE (fields.sender, sender);
  QCOMPARE (fields.grid, grid);
  QCOMPARE (fields.cq, cq);
}

QTEST_GUILESS_MAIN (TestDecodedMessageFields)

#include "test_decoded_message_fields.moc"
