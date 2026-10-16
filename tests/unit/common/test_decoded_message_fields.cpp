#include <QtTest>

#include "Decoder/decodedtext.h"

class TestDecodedMessageFields final : public QObject
{
  Q_OBJECT

private slots:
  void parsesMessage_data();
  void parsesMessage();
  void classifiesReceivedHashedMessages_data();
  void classifiesReceivedHashedMessages();
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
  QTest::newRow ("unresolved-destination") << "<...> V5/HB9SHD RR73" << "..." << "V5/HB9SHD" << "" << false;
  QTest::newRow ("unresolved-sender") << "K1ABC <...> RR73" << "K1ABC" << "..." << "" << false;
  QTest::newRow ("unresolved-both-calls") << "<...> <...> 590001 FN20QI" << "..." << "..." << "FN20" << false;
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

void TestDecodedMessageFields::classifiesReceivedHashedMessages_data()
{
  QTest::addColumn<QString> ("payload");
  QTest::addColumn<bool> ("standard");
  QTest::addColumn<QString> ("destination");
  QTest::addColumn<QString> ("sender");
  QTest::addColumn<QString> ("exchange");

  QTest::newRow ("namibia-type4") << "<...> V5/HB9SHD RR73" << true << "..." << "V5/HB9SHD" << "RR73";
  QTest::newRow ("type4-unresolved-sender") << "V5/HB9SHD <...> RR73" << true << "V5/HB9SHD" << "..." << "RR73";
  QTest::newRow ("type12-unresolved-destination") << "<...> W1AW RR73" << true << "..." << "W1AW" << "RR73";
  QTest::newRow ("type12-unresolved-sender") << "K1ABC <...> R-10" << true << "K1ABC" << "..." << "R-10";
  QTest::newRow ("type5-unresolved-destination") << "<...> <K1JT/P> 590001 FN20QI" << true << "..." << "K1JT/P" << "590001";
  QTest::newRow ("type5-unresolved-sender") << "<W3CCX> <...> R 590001 FN20QI" << true << "W3CCX" << "..." << "R";
  QTest::newRow ("type5-both-unresolved") << "<...> <...> 590001 FN20QI" << true << "..." << "..." << "590001";
  QTest::newRow ("dxpedition-unresolved-sender") << "K1ABC RR73; W2XX <...> -10" << true << "W2XX" << "..." << "-10";
  QTest::newRow ("dxpedition-unresolved-signoff-recipient") << "<...> RR73; K1ABC <W1AW> -10" << true << "K1ABC" << "W1AW" << "-10";
  QTest::newRow ("dxpedition-unresolved-report-recipient") << "K1ABC RR73; <...> <W1AW> -10" << true << "..." << "W1AW" << "-10";
  QTest::newRow ("dxpedition-hashed-signoff-recipient") << "<W2XX> RR73; K1ABC <...> -10" << true << "K1ABC" << "..." << "-10";
  QTest::newRow ("extra-type4-field") << "<...> V5/HB9SHD RR73 X" << false << "" << "" << "";
  QTest::newRow ("invalid-type4-tail") << "<...> V5/HB9SHD HELLO" << false << "" << "" << "";
  QTest::newRow ("extra-type12-field") << "<...> W1AW RR73 X" << false << "" << "" << "";
  QTest::newRow ("invalid-type12-report") << "<...> W1AW R-99" << false << "" << "" << "";
  QTest::newRow ("invalid-type5-serial") << "<...> <K1JT/P> 590000 FN20QI" << false << "" << "" << "";
  QTest::newRow ("invalid-type5-grid") << "<...> <K1JT/P> 590001 ZZ20QI" << false << "" << "" << "";
  QTest::newRow ("extra-dxpedition-field") << "K1ABC RR73; W2XX <...> -10 X" << false << "" << "" << "";
  QTest::newRow ("invalid-dxpedition-report") << "K1ABC RR73; W2XX <...> R-10" << false << "" << "" << "";
  QTest::newRow ("unclosed-placeholder") << "<... W1AW RR73" << false << "" << "" << "";
  QTest::newRow ("extra-bracket") << "<<...>> W1AW RR73" << false << "" << "" << "";
  QTest::newRow ("wrong-placeholder") << "<..> W1AW RR73" << false << "" << "" << "";
  QTest::newRow ("free-text-signoff") << "TNX QSO 73" << false << "" << "" << "";
}

void TestDecodedMessageFields::classifiesReceivedHashedMessages()
{
  QFETCH (QString, payload);
  QFETCH (bool, standard);
  QFETCH (QString, destination);
  QFETCH (QString, sender);
  QFETCH (QString, exchange);
  DecodedText const message {QString {"0605 -10  0.3 1500 ~  %1"}.arg (payload)};

  QCOMPARE (message.isStandardMessage (), standard);
  if (standard) {
    auto const words = message.messageWords ();
    QVERIFY (words.size () > 4);
    QCOMPARE (words.at (1), payload.contains ("RR73;")
              ? payload.section (' ', 0, 0).remove ('<').remove ('>') : QString {});
    QCOMPARE (words.at (2), destination);
    QCOMPARE (words.at (3), sender);
    QCOMPARE (words.at (4), exchange);
  }
}

QTEST_GUILESS_MAIN (TestDecodedMessageFields)

#include "test_decoded_message_fields.moc"
