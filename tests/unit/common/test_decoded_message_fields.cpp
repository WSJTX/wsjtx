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
  QTest::newRow ("unresolved-both-calls") << "<...> <...> 590001 FN20QI" << "..." << "..." << "FN20QI" << false;
  QTest::newRow ("later-grid") << "K1ABC W1AW R 520001 FN31AA" << "K1ABC" << "W1AW" << "FN31AA" << false;
  QTest::newRow ("incomplete") << "CQ TEST" << "CQ" << "TEST" << "" << true;
  QTest::newRow ("free-text") << "HELLO WORLD" << "HELLO" << "WORLD" << "" << false;
  QTest::newRow ("hashed-compound-signoff") << "<K1ABC> <W2XYZ/P> 73" << "K1ABC" << "W2XYZ/P" << "" << false;
  QTest::newRow ("hashed-nonstandard-report") << "<C8K> <OH6OKSA> R-13" << "C8K" << "OH6OKSA" << "" << false;
  QTest::newRow ("wspr-source") << "W1AW FN31 37" << "" << "W1AW" << "FN31" << false;
  QTest::newRow ("wspr-hashed-source") << "<...> FN31 37" << "" << "..." << "FN31" << false;
  QTest::newRow ("literal-signoff-punctuation") << "K1ABC W1AW 73!" << "K1ABC" << "W1AW" << "" << false;
  QTest::newRow ("overlength-diagnostic")
    << "$VERIFY$ C8K 1234567890 diagnostic text outside the protocol payload"
    << "" << "" << "" << false;
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
  if (message.endsWith ('!')) {
    QCOMPARE (fields.exchange, QString {"73!"});
    QVERIFY (!fields.completeTail);
    QVERIFY (!fields.reportPresent);
  }
  auto const rawWords = message.split (' ', Qt::SkipEmptyParts);
  QCOMPARE (fields.payloadWords.size (), message.size () > 37 ? 0 : rawWords.size ());
  if (message.contains ("<...>")) {
    QVERIFY (fields.destinationState == DecodedCallState::UnresolvedHash
             || fields.senderState == DecodedCallState::UnresolvedHash);
  }
  for (auto const& header : {QString {"0605 -10  0.3 1500 #* "}, QString {"060500 -10  0.3 1500 # "}}) {
    for (auto const& suffix : {QString {"f"}, QString {"f*"}, QString {"d22"},
                              QString {"d 2"}, QString {"a 2"}, QString {"q 2"}}) {
      DecodedText const decoded {header + message.leftJustified (22, ' ') + " " + suffix};
      QVERIFY (decoded.hasValidHeader ());
      QCOMPARE (decoded.payload (), message);
      QCOMPARE (decoded.fields ().destination, destination);
      QCOMPARE (decoded.fields ().sender, sender);
      QCOMPARE (decoded.fields ().grid, grid);
      QCOMPARE (decoded.annotations ().averageCount, suffix == "f" ? 0 : suffix == "f*" ? 10 : 2);
    }
  }
  if (fields.exchange == "73" || fields.exchange == "RR73" || fields.exchange == "R-13") {
    for (auto const& suffix : {QString {" ?"}, QString {" ? a3"}}) {
      DecodedText const confidence {"060500 -10  0.3 1500 ~  " + message + suffix};
      QCOMPARE (confidence.payload (), message);
      QVERIFY (confidence.annotations ().lowConfidence);
    }
  }
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
  QTest::newRow ("resolved-nonstandard-hound") << "C8K <OH6OKSA> R-13" << true << "C8K" << "OH6OKSA" << "R-13";
  QTest::newRow ("literal-question") << "WHY A3?" << false << "" << "" << "";
  QTest::newRow ("literal-annotation-token") << "TEST a3" << false << "" << "" << "";
  QTest::newRow ("literal-signoff-question") << "HELLO WORLD 73 ?" << false << "" << "" << "";
}

void TestDecodedMessageFields::classifiesReceivedHashedMessages()
{
  QFETCH (QString, payload);
  QFETCH (bool, standard);
  QFETCH (QString, destination);
  QFETCH (QString, sender);
  QFETCH (QString, exchange);
  for (auto const& time : {QString {"0605"}, QString {"060500"}}) {
    for (auto const& suffix : {QString {}, QString {"a3"}, QString {"? a7"}, QString {"q23#"}, QString {"q2*#"}}) {
      auto const line = time + " -10  0.3 1500 ~  " + payload.leftJustified (37, ' ')
        + (suffix.isEmpty () ? QString {} : " " + suffix);
      DecodedText message {line};
      QCOMPARE (message.string (), line);
      QVERIFY (message.hasValidHeader ());
      QCOMPARE (message.timeInSeconds (), 21900U);
      QCOMPARE (message.snr (), -10);
      QCOMPARE (message.frequencyOffset (), 1500);
      QCOMPARE (message.modeMarker (), QString {"~"});
      QCOMPARE (message.payload (), payload);
      QCOMPARE (message.isStandardMessage (), standard);
      QCOMPARE (message.annotations ().lowConfidence, suffix.startsWith ('?'));
      QCOMPARE (message.annotations ().ap, suffix.contains ("a7") ? 7 : suffix.contains ("a3") ? 3 : 0);
      QCOMPARE (message.annotations ().copied, suffix.endsWith ('#'));
      QCOMPARE (message.annotations ().averageCount, suffix.startsWith ('q') ? (suffix.contains ('*') ? 10 : 3) : 0);
      QCOMPARE (message.withoutAnnotations (), time + " -10  0.3 1500 ~  " + payload);
      if (standard) {
        auto const words = message.messageWords ();
        QVERIFY (words.size () > 4);
        auto const acknowledged = payload.contains ("RR73;")
          ? payload.section (' ', 0, 0).remove ('<').remove ('>') : QString {};
        QCOMPARE (words.at (1), acknowledged);
        QCOMPARE (words.at (2), destination);
        QCOMPARE (words.at (3), sender);
        QCOMPARE (words.at (4), exchange);
        QCOMPARE (message.fields ().destination, destination);
        QCOMPARE (message.fields ().sender, sender);
        QCOMPARE (message.fields ().exchange, exchange);
        QCOMPARE (message.fields ().acknowledgedRecipient, acknowledged);
        QVERIFY (!message.fields ().hasMalformedCalls);
        QString legacySender;
        QString legacyExchange;
        message.deCallAndGrid (legacySender, legacyExchange);
        QCOMPARE (legacySender, sender);
        QCOMPARE (legacyExchange, exchange == "R" ? message.fields ().exchangeWords.value (1) : exchange);
        QString receivedReport;
        QVERIFY (message.report (destination, sender, receivedReport));
        auto expectedReport = exchange;
        if (expectedReport.startsWith ('R')) expectedReport.remove (0, 1);
        bool numeric = false;
        auto const value = expectedReport.toInt (&numeric);
        if (!numeric || value < -50 || value >= 50) expectedReport.clear ();
        QCOMPARE (receivedReport, expectedReport);
      }
      message.clear ();
      QVERIFY (message.string ().isEmpty ());
      QVERIFY (message.payload ().isEmpty ());
      QVERIFY (message.fields ().sender.isEmpty ());
      QVERIFY (!message.hasValidHeader ());
      QVERIFY (!message.isStandardMessage ());
    }
  }
}

QTEST_GUILESS_MAIN (TestDecodedMessageFields)

#include "test_decoded_message_fields.moc"
